/*
 * ack_delay_proxy.c -- a one-directional delay line in front of a fleet
 * arm's RDP port. BACKLOG #79, test layer 1.
 *
 * WHAT IT IS FOR. #79 claims the fif = 1 throughput tail is a producer
 * ack that xrdp withholds until the CLIENT's frame ack re-opens
 * xrdp_gfx_ack_window_open(). That claim was inferred from traces. The
 * way to confirm it by INTERVENTION -- without touching the server
 * binary, the client binary, or any config -- is to make the client's
 * acks arrive later on purpose and watch the withheld-credit
 * distribution follow. If the theory is right, delaying the
 * client->server direction by D ms moves withheld by D and drives the
 * stall fraction to ~100 %. If it is wrong, it does not.
 *
 * WHAT IT DOES. Accepts on a loopback port, connects to the arm's port,
 * and forwards both directions. server->client is forwarded as soon as
 * poll() says it can be (this is the 4K video direction: it must not
 * become the bottleneck). client->server is queued per read and
 * released D ms after it arrived -- so every byte the client sends,
 * acks included, reaches the server D ms late.
 *
 * WHAT IT IS NOT. It does not parse RDP: the connection is TLS and the
 * acks are not visible. It delays the whole client->server direction.
 * In an oracle-client measurement session that direction carries frame
 * acks and nothing else of consequence (no input is generated), which
 * is what makes the crude instrument sufficient here.
 *
 * ITS OWN TELEMETRY (quality gate 2 -- a knob that was set is not a
 * knob that applied). On exit each connection reports bytes moved each
 * way and the distribution of the delay it ACTUALLY applied, measured
 * from arrival to release. A leg whose applied delay does not match D
 * is a harness fault, not a result. The server side has an independent
 * check of the same thing: the egress -> cliack latency in the perf
 * ring must rise by D too.
 *
 * Build:  gcc -O2 -Wall -o ack_delay_proxy ack_delay_proxy.c
 * Usage:  ack_delay_proxy -l 41000 -r 40033 -d 20 [-F 10] [-H 127.0.0.1]
 *         -d  delay client->server by this many ms
 *         -F  stop forwarding client->server entirely after this many
 *             seconds (a client that stops acking but keeps reading)
 *
 * Deliberately standalone: no xrdp headers, no log.c (this runs on the
 * measurement path and log.c writes unbuffered under a global mutex --
 * CLAUDE.md coding rule 5). It prints only at connection open/close.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define S2C_BUF (256 * 1024)
#define C2S_READ (64 * 1024)
/* client->server on an oracle session is acks only: a few hundred bytes
   per frame. 8 MB of queue is ~40 s of it, and refusing to read further
   (instead of growing) keeps a runaway from being invisible. */
#define C2S_CAP (8 * 1024 * 1024)
#define MAX_SAMPLES 200000
#define MAX_CONNS 64

struct chunk
{
    struct chunk *next;
    long long got_ns;
    long long due_ns;
    int len;
    int off;
    char data[1];
};

static volatile int g_stop = 0;
static pid_t g_kids[MAX_CONNS];

/*****************************************************************************/
static void
on_term(int sig)
{
    (void)sig;
    g_stop = 1;
}

/*****************************************************************************/
/* deliberately WITHOUT SA_RESTART: the point of the signal is to break
   accept()/poll() out, so the child can print its applied-delay
   telemetry on the way down. signal() installs SA_RESTART on glibc and
   the first version of this sat in accept() through SIGTERM. */
static void
install_handler(int sig, void (*fn)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, NULL);
}

/*****************************************************************************/
static long long
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

/*****************************************************************************/
static int
set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);

    if (fl < 0)
    {
        return -1;
    }
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/*****************************************************************************/
static void
set_nodelay(int fd)
{
    int one = 1;

    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

/*****************************************************************************/
static int
cmp_ll(const void *a, const void *b)
{
    long long x = *(const long long *)a;
    long long y = *(const long long *)b;

    return (x > y) - (x < y);
}

/*****************************************************************************/
static int
connect_remote(const char *host, int port)
{
    struct sockaddr_in sa;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
    {
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    sa.sin_addr.s_addr = inet_addr(host);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

/*****************************************************************************/
/* one accepted connection, in its own process.
 *
 * freeze_s > 0 stops forwarding client->server entirely that many
 * seconds after the connection opens, and never resumes: the client
 * keeps RECEIVING (the video direction is untouched, so this is not
 * TCP backpressure) but its acks stop arriving. That is the safety
 * half of #79's validation gate -- what the ack window's own comment
 * says it is there for ("a client that stops acking still stops the
 * producer"). D and freeze compose: the delay applies until the
 * freeze instant. */
static void
handle_conn(int cfd, const char *rhost, int rport, int delay_ms, int id,
            int freeze_s)
{
    long long freeze_at = 0;
    int frozen = 0;
    struct chunk *head = NULL;
    struct chunk *tail = NULL;
    long long *samples;
    long long c2s_bytes = 0;
    long long s2c_bytes = 0;
    long long q_bytes = 0;
    long long t_open;
    int nsamples = 0;
    int sfd;
    int s2c_len = 0;
    int s2c_off = 0;
    int c_eof = 0;
    int s_eof = 0;
    int c_shut = 0;
    int s_shut = 0;
    char *s2c;

    sfd = connect_remote(rhost, rport);
    if (sfd < 0)
    {
        fprintf(stderr, "proxy[%d]: connect %s:%d failed: %s\n",
                id, rhost, rport, strerror(errno));
        close(cfd);
        return;
    }
    s2c = (char *)malloc(S2C_BUF);
    samples = (long long *)malloc(sizeof(long long) * MAX_SAMPLES);
    if (s2c == NULL || samples == NULL)
    {
        fprintf(stderr, "proxy[%d]: out of memory\n", id);
        return;
    }
    set_nonblock(cfd);
    set_nonblock(sfd);
    set_nodelay(cfd);
    set_nodelay(sfd);
    t_open = now_ns();
    if (freeze_s > 0)
    {
        freeze_at = t_open + (long long)freeze_s * 1000000000LL;
    }
    fprintf(stderr, "proxy[%d]: open -> %s:%d delay=%dms freeze=%ds\n",
            id, rhost, rport, delay_ms, freeze_s);

    while (!g_stop)
    {
        struct pollfd p[2];
        long long now = now_ns();
        int timeout = -1;
        int n;

        p[0].fd = cfd;
        p[0].events = 0;
        p[0].revents = 0;
        p[1].fd = sfd;
        p[1].events = 0;
        p[1].revents = 0;
        if (!c_eof && q_bytes < C2S_CAP)
        {
            p[0].events |= POLLIN;
        }
        if (s2c_off < s2c_len)
        {
            p[0].events |= POLLOUT;
        }
        if (!s_eof && s2c_off >= s2c_len)
        {
            p[1].events |= POLLIN;
        }
        if (freeze_at != 0 && now >= freeze_at && !frozen)
        {
            frozen = 1;
            fprintf(stderr, "proxy[%d]: FROZEN at %.1fs -- client->server "
                    "stops here; the video direction keeps flowing\n",
                    id, (double)(now - t_open) / 1e9);
        }
        if (head != NULL && !frozen)
        {
            if (head->due_ns <= now)
            {
                p[1].events |= POLLOUT;
            }
            else
            {
                timeout = (int)((head->due_ns - now + 999999LL) / 1000000LL);
            }
        }
        else if (freeze_at != 0 && !frozen)
        {
            /* wake at the freeze instant even with nothing queued */
            long long dt = freeze_at - now;

            if (dt > 0 && (timeout < 0 || dt / 1000000LL < timeout))
            {
                timeout = (int)(dt / 1000000LL) + 1;
            }
        }
        /* both directions finished and flushed */
        if (c_eof && (head == NULL || frozen) && s_eof
            && s2c_off >= s2c_len)
        {
            break;
        }
        if (p[0].events == 0 && p[1].events == 0 && timeout < 0)
        {
            break;
        }
        n = poll(p, 2, timeout);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        now = now_ns();

        /* client -> queue */
        if ((p[0].revents & (POLLIN | POLLHUP | POLLERR)) != 0 && !c_eof)
        {
            char buf[C2S_READ];
            int r = (int)recv(cfd, buf, sizeof(buf), 0);

            if (r > 0)
            {
                struct chunk *ch = (struct chunk *)
                                   malloc(sizeof(struct chunk) + r);

                if (ch == NULL)
                {
                    break;
                }
                ch->next = NULL;
                ch->got_ns = now;
                ch->due_ns = now + (long long)delay_ms * 1000000LL;
                ch->len = r;
                ch->off = 0;
                memcpy(ch->data, buf, r);
                if (tail == NULL)
                {
                    head = ch;
                    tail = ch;
                }
                else
                {
                    tail->next = ch;
                    tail = ch;
                }
                q_bytes += r;
                c2s_bytes += r;
            }
            else if (r == 0)
            {
                c_eof = 1;
            }
            else if (errno != EAGAIN && errno != EWOULDBLOCK
                     && errno != EINTR)
            {
                c_eof = 1;
            }
        }

        /* queue -> server, when due (never, once frozen) */
        while (head != NULL && head->due_ns <= now && !frozen)
        {
            int w = (int)send(sfd, head->data + head->off,
                              head->len - head->off, MSG_NOSIGNAL);

            if (w > 0)
            {
                head->off += w;
                q_bytes -= w;
                if (head->off >= head->len)
                {
                    struct chunk *done = head;

                    if (nsamples < MAX_SAMPLES)
                    {
                        samples[nsamples++] = now - done->got_ns;
                    }
                    head = head->next;
                    if (head == NULL)
                    {
                        tail = NULL;
                    }
                    free(done);
                }
            }
            else
            {
                if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK
                    && errno != EINTR)
                {
                    s_eof = 1;
                }
                break;
            }
        }

        /* server -> buffer */
        if ((p[1].revents & (POLLIN | POLLHUP | POLLERR)) != 0 && !s_eof
            && s2c_off >= s2c_len)
        {
            int r = (int)recv(sfd, s2c, S2C_BUF, 0);

            if (r > 0)
            {
                s2c_len = r;
                s2c_off = 0;
                s2c_bytes += r;
            }
            else if (r == 0)
            {
                s_eof = 1;
            }
            else if (errno != EAGAIN && errno != EWOULDBLOCK
                     && errno != EINTR)
            {
                s_eof = 1;
            }
        }

        /* buffer -> client */
        while (s2c_off < s2c_len)
        {
            int w = (int)send(cfd, s2c + s2c_off, s2c_len - s2c_off,
                              MSG_NOSIGNAL);

            if (w > 0)
            {
                s2c_off += w;
            }
            else
            {
                if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK
                    && errno != EINTR)
                {
                    c_eof = 1;
                    s_eof = 1;
                }
                break;
            }
        }

        if (c_eof && (head == NULL || frozen) && !s_shut)
        {
            shutdown(sfd, SHUT_WR);
            s_shut = 1;
        }
        if (s_eof && s2c_off >= s2c_len && !c_shut)
        {
            shutdown(cfd, SHUT_WR);
            c_shut = 1;
        }
    }

    {
        double secs = (double)(now_ns() - t_open) / 1e9;
        double mean = 0.0;
        int i;

        for (i = 0; i < nsamples; i++)
        {
            mean += (double)samples[i];
        }
        if (nsamples > 0)
        {
            mean /= (double)nsamples;
        }
        qsort(samples, nsamples, sizeof(long long), cmp_ll);
        fprintf(stderr, "proxy[%d]: close after %.1fs  c2s=%lld B  "
                "s2c=%lld B (%.1f MB/s)\n", id, secs, c2s_bytes, s2c_bytes,
                secs > 0 ? (double)s2c_bytes / secs / 1e6 : 0.0);
        fprintf(stderr, "proxy[%d]: applied delay n=%d  want=%dms  "
                "mean=%.2f p50=%.2f p90=%.2f max=%.2f ms\n", id, nsamples,
                delay_ms, mean / 1e6,
                nsamples ? (double)samples[nsamples / 2] / 1e6 : 0.0,
                nsamples ? (double)samples[9 * nsamples / 10] / 1e6 : 0.0,
                nsamples ? (double)samples[nsamples - 1] / 1e6 : 0.0);
    }
    close(cfd);
    close(sfd);
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    struct sockaddr_in sa;
    const char *rhost = "127.0.0.1";
    int lport = 0;
    int rport = 0;
    int delay_ms = 0;
    int freeze_s = 0;
    int lfd;
    int one = 1;
    int id = 0;
    int i;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-l") == 0 && i + 1 < argc)
        {
            lport = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-r") == 0 && i + 1 < argc)
        {
            rport = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc)
        {
            delay_ms = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc)
        {
            freeze_s = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "-H") == 0 && i + 1 < argc)
        {
            rhost = argv[++i];
        }
        else
        {
            fprintf(stderr, "usage: %s -l <listen> -r <remote> "
                    "[-H host] [-d delay_ms] [-F freeze_after_s]\n",
                    argv[0]);
            return 1;
        }
    }
    if (lport <= 0 || rport <= 0 || delay_ms < 0 || freeze_s < 0)
    {
        fprintf(stderr, "usage: %s -l <listen> -r <remote> "
                "[-H host] [-d delay_ms] [-F freeze_after_s]\n", argv[0]);
        return 1;
    }
    install_handler(SIGTERM, on_term);
    install_handler(SIGINT, on_term);
    signal(SIGPIPE, SIG_IGN);

    lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
    {
        perror("socket");
        return 1;
    }
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)lport);
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(lfd, (struct sockaddr *)&sa, sizeof(sa)) < 0)
    {
        perror("bind");
        return 1;
    }
    if (listen(lfd, 8) < 0)
    {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "proxy: listening 127.0.0.1:%d -> %s:%d delay=%dms "
            "freeze=%ds (client->server only)\n", lport, rhost, rport,
            delay_ms, freeze_s);
    while (!g_stop)
    {
        int cfd = accept(lfd, NULL, NULL);
        pid_t pid;

        if (cfd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        id++;
        pid = fork();
        if (pid == 0)
        {
            close(lfd);
            handle_conn(cfd, rhost, rport, delay_ms, id, freeze_s);
            _exit(0);
        }
        if (pid > 0 && id <= MAX_CONNS)
        {
            g_kids[id - 1] = pid;
        }
        close(cfd);
        while (waitpid(-1, NULL, WNOHANG) > 0)
        {
            /* reap finished connections */
        }
    }
    /* pass the signal on: the children are the ones holding the
       telemetry, and they are sitting in poll() */
    for (i = 0; i < MAX_CONNS; i++)
    {
        if (g_kids[i] > 0)
        {
            kill(g_kids[i], SIGTERM);
        }
    }
    for (i = 0; i < MAX_CONNS; i++)
    {
        if (g_kids[i] > 0)
        {
            waitpid(g_kids[i], NULL, 0);
        }
    }
    close(lfd);
    return 0;
}
