/*
 * pump_split_probe -- split the worker's `pump` bracket into FEED and
 * ENCODE, using the REAL encoder stack, inside a deployed arm's pod.
 *
 * WHY THIS EXISTS. xrdp/xrdp_encoder_ffmpeg.c contains no PERF_TRACE at
 * all, so `pump_beg -> pump_end` is one opaque block covering three
 * different things:
 *
 *   FEED    vmsplice()ing 2 x 13.824 MB of NV12 through two 1 MiB pipes,
 *           driven by the SAME thread that is waiting, in the SAME poll
 *           loop (pump_set -> pump_service -> feed_vmsplice).
 *   ENCODE  the children actually producing the two pictures.
 *   DRAIN   reading the coded bytes back and framing them.
 *
 * "Is ffmpeg as unblocked as it can practically be" cannot be answered
 * without that split. BACKLOG #78 is to instrument the deployed path
 * properly; this is the cheap standalone that does not need a session, a
 * client, an encode of real content or a wire.
 *
 * WHAT IT IS NOT. It is not the deployed pipeline and its numbers are
 * not a decomposition of a deployed measurement -- on 2026-08-02 it read
 * 9.3-10.6 ms against a deployed 16.6 ms (x014) and 26.7 ms (x015). The
 * gap is the finding, not a defect in the probe. Run it INSIDE the arm's
 * pod so the ffmpeg binary, the VAAPI driver and the encoder_args are
 * the real ones (strict-honesty rule: no stand-ins).
 *
 * It mirrors xrdp_ffmpeg_avc444_pump_pairs()/pump_set() path for path:
 * two children, one poll set, one shared deadline, non-blocking vmsplice
 * from borrowed pages, stdout drained every round.
 *
 * USAGE (from the dev box):
 *   gcc -O2 -o pump_split_probe pump_split_probe.c
 *   kubectl -n bisect-matrix cp pump_split_probe <pod>:/tmp/p
 *   kubectl -n bisect-matrix exec <pod> -- /tmp/p [frames] [renderdev]
 *
 * SHAPES, selected by environment variable:
 *   (none)      two children, ONE poll set  -- what HEAD does
 *   SERIAL=1    two children, strictly one at a time -- the
 *               dev/avc444_metablock_checkpoint aux_intra_leaf shape.
 *               Measured 19.0 ms vs 9.7 ms: the 2.0x that #45 step 5's
 *               shared poll set bought.
 *   ONECHILD=1  one child, BOTH pictures through one pipe -- the
 *               metablock DEFAULT shape. *** ITS NUMBER IS NOT A
 *               RESULT. *** It measured 147 ms, tightly distributed, and
 *               no knob moved it (-async_depth 4, extra_hw_frames=8).
 *               15x is not a serialisation factor and there is no
 *               mechanism for it, so assume a defect in THIS driver
 *               until one is found. Do not quote it as a property of
 *               that branch.
 *   PICS1=1     one child, one picture -- isolates a single child.
 *   ADEPTH=n    override -async_depth.
 *   EXTRAHW=1   hwupload=extra_hw_frames=8.
 *
 * LEAVE NOTHING RUNNING. This spawns ffmpeg children against the shared
 * render node; it kills them on exit, and the caller should remove the
 * binary from the pod afterwards.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <signal.h>

#define NKIDS 2
#define PIPE_SZ (1024 * 1024)
#define READ_CHUNK 65536

static int g_w = 3840;
static int g_h = 2400;
static int g_frames = 60;
static int g_serial = 0;
static int g_one = 0;   /* metablock default: ONE child, both pictures */
static int g_nk = NKIDS;

struct kid
{
    pid_t pid;
    int in_fd;
    int out_fd;
    const unsigned char *iov_base;
    const unsigned char *aux;
    size_t iov_len;
    size_t iov_off;
    int pending;
    int second;
    /* rolling tail so a start code split across reads is still seen */
    unsigned char tail[3];
    int vcl;          /* VCL NALs seen so far */
    int vcl_target;   /* what "this frame is out" means */
    long long t_fed;  /* ns: input fully accepted by the pipe */
    long long t_out;  /* ns: this frame's picture seen */
};

static long long
now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int
spawn(struct kid *k, const char *dev)
{
    int inp[2];
    int outp[2];
    char size[64];
    char gop[16];
    pid_t pid;

    snprintf(size, sizeof(size), "%dx%d", g_w, g_h);
    snprintf(gop, sizeof(gop), "%d", 240);
    if (pipe(inp) != 0 || pipe(outp) != 0)
    {
        return 1;
    }
    pid = fork();
    if (pid < 0)
    {
        return 1;
    }
    if (pid == 0)
    {
        char *argv[] =
        {
            "ffmpeg", "-hide_banner", "-loglevel", "error",
            "-f", "rawvideo", "-pix_fmt", "nv12", "-s", size, "-r", "60",
            "-i", "pipe:0",
            /* verbatim from PR-demo/mac_bisect_matrix/gfx/x014.toml */
            "-vaapi_device", (char *)dev,
            "-vf", (getenv("EXTRAHW") ? "format=nv12,hwupload=extra_hw_frames=8" : "format=nv12,hwupload"),
            "-c:v", "h264_vaapi",
            "-rc_mode", "CQP", "-qp", "20",
            "-bf", "0", "-async_depth", (getenv("ADEPTH") ? getenv("ADEPTH") : "1"),
            "-g", gop,
            "-f", "h264", "pipe:1", NULL
        };

        dup2(inp[0], 0);
        dup2(outp[1], 1);
        close(inp[0]);
        close(inp[1]);
        close(outp[0]);
        close(outp[1]);
        execvp("ffmpeg", argv);
        _exit(127);
    }
    close(inp[0]);
    close(outp[1]);
    fcntl(inp[1], F_SETFL, O_NONBLOCK);
    fcntl(outp[0], F_SETFL, O_NONBLOCK);
    fcntl(inp[1], F_SETPIPE_SZ, PIPE_SZ);
    k->pid = pid;
    k->in_fd = inp[1];
    k->out_fd = outp[0];
    k->pending = 0;
    k->vcl = 0;
    k->vcl_target = 0;
    memset(k->tail, 0xff, sizeof(k->tail));
    return 0;
}

/* count VCL NAL start codes; the picture is "out" when its first byte
 * lands, which is what pk_available() effectively observes one packet
 * later -- the difference is the child's write of ~1.5 MB */
static void
scan_vcl(struct kid *k, const unsigned char *b, int n)
{
    unsigned char w[4];
    int i;

    for (i = 0; i < n; i++)
    {
        w[0] = k->tail[0];
        w[1] = k->tail[1];
        w[2] = k->tail[2];
        w[3] = b[i];
        if (w[0] == 0 && w[1] == 0 && w[2] == 1)
        {
            int t = w[3] & 0x1f;

            if (t == 1 || t == 5)
            {
                k->vcl++;
            }
        }
        k->tail[0] = k->tail[1];
        k->tail[1] = k->tail[2];
        k->tail[2] = b[i];
    }
}

static int
feed(struct kid *k)
{
    struct iovec iov;
    ssize_t n;

    if (!k->pending)
    {
        return 0;
    }
    iov.iov_base = (void *)(k->iov_base + k->iov_off);
    iov.iov_len = k->iov_len - k->iov_off;
    n = vmsplice(k->in_fd, &iov, 1, SPLICE_F_NONBLOCK);
    if (n > 0)
    {
        k->iov_off += (size_t)n;
        if (k->iov_off >= k->iov_len)
        {
            k->iov_off = 0;
            if (k->second)
            {
                /* metablock encode_pair(): main and aux are pushed into
                 * the SAME child's iovec list, one pipe, in order */
                k->second = 0;
                k->iov_base = k->aux;
            }
            else
            {
                k->pending = 0;
                k->t_fed = now_ns();
            }
        }
        return 0;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
    {
        return 1;
    }
    return 0;
}

static int
drain(struct kid *k)
{
    unsigned char buf[READ_CHUNK];
    ssize_t n;

    for (;;)
    {
        n = read(k->out_fd, buf, sizeof(buf));
        if (n > 0)
        {
            scan_vcl(k, buf, (int)n);
            if (k->t_out == 0 && k->vcl >= k->vcl_target)
            {
                k->t_out = now_ns();
            }
            continue;
        }
        if (n == 0)
        {
            return -1;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return 0;
        }
        if (errno == EINTR)
        {
            continue;
        }
        return -1;
    }
}

static int
cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return (x > y) - (x < y);
}

static void
report(const char *name, double *v, int n)
{
    double s = 0.0;
    int i;

    if (n < 1)
    {
        printf("%-46s none\n", name);
        return;
    }
    for (i = 0; i < n; i++)
    {
        s += v[i];
    }
    qsort(v, n, sizeof(double), cmp_d);
    printf("%-46s n=%4d mean=%7.3f p50=%7.3f p90=%7.3f max=%7.3f\n",
           name, n, s / n, v[n / 2], v[(int)(0.9 * (n - 1))], v[n - 1]);
}

int
main(int argc, char **argv)
{
    const char *dev = "/dev/dri/renderD128";
    struct kid kids[NKIDS];
    unsigned char *pic; unsigned char *pic2;
    size_t nv12;
    double *d_feed;
    double *d_enc;
    double *d_pump;
    int warm = 10;
    int i;
    int f;
    int nrec = 0;

    if (argc > 1)
    {
        g_frames = atoi(argv[1]);
    }
    if (argc > 2)
    {
        dev = argv[2];
    }
    g_serial = getenv("SERIAL") != NULL;
    g_one = getenv("ONECHILD") != NULL;
    if (getenv("PICS1") != NULL) { g_one = 2; }
    if (g_one) { g_nk = 1; }
    signal(SIGPIPE, SIG_IGN);
    nv12 = (size_t)g_w * g_h + (size_t)g_w * (g_h / 2);
    pic = (unsigned char *)malloc(nv12); pic2 = (unsigned char *)malloc(nv12);
    if (pic == NULL)
    {
        return 1;
    }
    /* content does not matter for the split, but must not be constant --
     * a flat picture encodes to nothing and understates the encode */
    for (i = 0; i < (int)nv12; i++)
    {
        pic[i] = (unsigned char)((i * 37 + (i >> 11) * 91) & 0xff); pic2[i] = (unsigned char)((i * 53 + (i >> 10) * 17) & 0xff);
    }
    memset(kids, 0, sizeof(kids));
    for (i = 0; i < g_nk; i++)
    {
        if (spawn(&kids[i], dev) != 0)
        {
            printf("spawn failed\n");
            return 1;
        }
    }
    d_feed = (double *)malloc(sizeof(double) * g_frames);
    d_enc = (double *)malloc(sizeof(double) * g_frames);
    d_pump = (double *)malloc(sizeof(double) * g_frames);
    printf("probe: %dx%d NV12 %zu bytes/view, %d children, pipe %d KiB, "
           "%d frames (%d warm-up discarded)%s\n",
           g_w, g_h, nv12, g_nk, PIPE_SZ / 1024, g_frames, warm,
           g_one ? "  [ONE child, both pictures -- metablock default]" :
           (g_serial ? "  [SERIAL: one child at a time]" : "  [one poll set]"));

    for (f = 0; f < g_frames; f++)
    {
        long long t0;
        long long deadline;
        int done;

        /* SUBMIT: queue the borrowed picture, exactly as
         * xrdp_ffmpeg_avc444_submit_pair does -- no I/O here */
        for (i = 0; i < g_nk; i++)
        {
            kids[i].iov_base = (i == 0) ? pic : pic2;
            kids[i].iov_len = nv12;
            kids[i].iov_off = 0;
            kids[i].aux = pic2;
            kids[i].pending = 1;
            kids[i].second = (g_one == 1);   /* aux picture still to push */
            kids[i].t_fed = 0;
            kids[i].t_out = 0;
            kids[i].vcl_target = kids[i].vcl + ((g_one == 1) ? 2 : 1);
        }
        /* PUMP */
        t0 = now_ns();
        deadline = t0 + 5000000000LL;
        for (;;)
        {
            struct pollfd pfd[2 * NKIDS];
            int nfds = 0;
            int slot_in[NKIDS];
            int want_write = 0;

            done = 1;
            for (i = 0; i < g_nk; i++)
            {
                if (kids[i].t_out == 0)
                {
                    done = 0;
                }
            }
            if (done || now_ns() >= deadline)
            {
                break;
            }
            for (i = 0; i < g_nk; i++)
            {
                slot_in[i] = -1;
                /* SERIAL: mirror the metablock aux_intra_leaf path --
                 * encode_single(main) runs to completion before
                 * encode_single(aux) is even fed. */
                if (g_serial && i > 0 && kids[0].t_out == 0)
                {
                    continue;
                }
                if (kids[i].pending)
                {
                    pfd[nfds].fd = kids[i].in_fd;
                    pfd[nfds].events = POLLOUT;
                    slot_in[i] = nfds;
                    nfds++;
                    want_write = 1;
                }
                pfd[nfds].fd = kids[i].out_fd;
                pfd[nfds].events = POLLIN;
                nfds++;
            }
            poll(pfd, nfds, want_write ? 100 : 10);
            for (i = 0; i < g_nk; i++)
            {
                if (slot_in[i] >= 0 &&
                        (pfd[slot_in[i]].revents & POLLOUT))
                {
                    if (feed(&kids[i]) != 0)
                    {
                        printf("feed error\n");
                        return 1;
                    }
                }
                if (g_serial && i > 0 && kids[0].t_out == 0)
                {
                    continue;
                }
                if (drain(&kids[i]) != 0)
                {
                    printf("child %d EOF/error at frame %d\n", i, f);
                    return 1;
                }
            }
        }
        if (!done)
        {
            printf("frame %d TIMED OUT\n", f);
            return 1;
        }
        if (f >= warm)
        {
            long long fed = kids[0].t_fed;
            long long out = kids[0].t_out;

            if (g_nk > 1)
            {
                fed = kids[0].t_fed > kids[1].t_fed
                      ? kids[0].t_fed : kids[1].t_fed;
                out = kids[0].t_out > kids[1].t_out
                      ? kids[0].t_out : kids[1].t_out;
            }

            d_feed[nrec] = (double)(fed - t0) / 1e6;
            d_enc[nrec] = (double)(out - fed) / 1e6;
            d_pump[nrec] = (double)(out - t0) / 1e6;
            nrec++;
        }
    }
    printf("\n");
    report("FEED   pump_beg -> both inputs in the pipes", d_feed, nrec);
    report("ENCODE both fed -> both pictures out", d_enc, nrec);
    report("PUMP   total (feed + encode)", d_pump, nrec);
    for (i = 0; i < g_nk; i++)
    {
        kill(kids[i].pid, SIGKILL);
        waitpid(kids[i].pid, NULL, 0);
    }
    return 0;
}
