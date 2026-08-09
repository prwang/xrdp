/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2024
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * WHAT THIS ANSWERS, and why it exists outside xrdp entirely.
 *
 * The AVC444 backend hands each raw picture to its ffmpeg child through
 * a pipe: xrdp calls vmsplice(), which moves page REFERENCES and never
 * copies, and the child's read() copies the bytes out. On 2026-08-08 a
 * fleet run at 3840x2400 measured that handover taking 7.5-9.1 ms for a
 * 13.82 MB picture -- about 1.5-1.8 GB/s -- which is slow enough for a
 * single copy that it is worth asking whether the mechanism itself is
 * the cost rather than the encoder being busy behind it.
 *
 * That question cannot be settled from inside xrdp, because inside xrdp
 * the reader is ffmpeg and everything is entangled with the encode. So
 * this reproduces JUST the handover: a parent that vmsplices a buffer
 * into a pipe exactly the way xrdp_encoder_ffmpeg.c's feed_vmsplice()
 * does, and a forked child that reads it and throws it away. No X
 * server, no encoder, no GPU, no session -- one source file and a fork.
 *
 * ARMS, and what each one isolates:
 *
 *   memcpy            the box's own single-threaded copy bandwidth for
 *                     the same buffer. The ceiling everything else is
 *                     read against; without it a GB/s figure means
 *                     nothing.
 *   vmsplice          the shipped mechanism, swept over pipe size and
 *                     over the READER's chunk size. The reader's chunk
 *                     size is the interesting variable: ffmpeg reads
 *                     its input through an avio buffer, and if that
 *                     buffer is small the handover becomes hundreds of
 *                     syscalls and pipe wakeups per picture.
 *   write             the same transfer with an ordinary write()
 *                     instead of vmsplice, which is what tells you
 *                     whether the zero-copy side is buying anything.
 *
 * Every arm moves exactly the same number of bytes and is timed the
 * same way: from the first byte offered to the pipe until the child has
 * read the last one and said so on a second pipe. The child's
 * acknowledgement is what makes this the HANDOVER time and not just the
 * writer's time -- a writer that fills a pipe and returns has not
 * transferred anything.
 *
 * Build:  cc -O2 -o vmsplice_pipe_bench tools/vmsplice_pipe_bench.c
 * Run:    ./vmsplice_pipe_bench [bytes] [iterations]
 *         defaults: 13824000 (3840x2400 NV12) and 30
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_BYTES 13824000      /* 3840x2400 NV12, one picture */
#define DEFAULT_ITERS 30

/*****************************************************************************/
static double
now_s(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/*****************************************************************************/
/* The child: read exactly nbytes in chunks of chunk, then write one
 * byte back so the parent can time the HANDOVER rather than its own
 * write. Loops for iters pictures. */
static void
child_reader(int in_fd, int ack_fd, long long nbytes, int chunk, int iters)
{
    char *buf;
    int i;

    buf = (char *)malloc((size_t)chunk);
    if (buf == NULL)
    {
        _exit(1);
    }
    for (i = 0; i < iters; i++)
    {
        long long left = nbytes;

        while (left > 0)
        {
            size_t want = (size_t)((left < chunk) ? left : chunk);
            ssize_t n = read(in_fd, buf, want);

            if (n > 0)
            {
                left -= n;
            }
            else if (n == 0)
            {
                _exit(2);       /* writer closed early */
            }
            else if (errno != EINTR)
            {
                _exit(3);
            }
        }
        if (write(ack_fd, "x", 1) != 1)
        {
            _exit(4);
        }
    }
    _exit(0);
}

/*****************************************************************************/
/* One arm. use_vmsplice selects the mechanism; pipe_sz 0 leaves the
 * pipe at its system default. Returns GB/s, or -1 on failure. */
static double
run_arm(const char *label, const unsigned char *src, long long nbytes,
        int iters, int pipe_sz, int chunk, int use_vmsplice)
{
    int datafd[2];
    int ackfd[2];
    pid_t pid;
    double t0;
    double t1;
    double total = 0.0;
    int got_sz = 0;
    int resize_failed = 0;
    int i;

    if (pipe(datafd) != 0 || pipe(ackfd) != 0)
    {
        return -1.0;
    }
    if (pipe_sz > 0)
    {
        /* exactly as spawn_child() does -- and, exactly as it does, the
         * return value is available and interesting. It FAILS with
         * EPERM whenever this uid is over fs/pipe-user-pages-soft, and
         * the pipe then stays at whatever minimum the kernel handed
         * out. xrdp does not check it, so that failure is silent. */
        if (fcntl(datafd[1], F_SETPIPE_SZ, pipe_sz) < 0)
        {
            resize_failed = errno;
        }
    }
    got_sz = fcntl(datafd[1], F_GETPIPE_SZ);

    pid = fork();
    if (pid < 0)
    {
        return -1.0;
    }
    if (pid == 0)
    {
        close(datafd[1]);
        close(ackfd[0]);
        child_reader(datafd[0], ackfd[1], nbytes, chunk, iters);
        _exit(5);
    }
    close(datafd[0]);
    close(ackfd[1]);

    for (i = 0; i < iters; i++)
    {
        long long off = 0;
        char ack;

        t0 = now_s();
        while (off < nbytes)
        {
            ssize_t n;

            if (use_vmsplice)
            {
                struct iovec iov;

                iov.iov_base = (void *)(src + off);
                iov.iov_len = (size_t)(nbytes - off);
                /* SPLICE_F_NONBLOCK and never SPLICE_F_GIFT -- the
                 * pages belong to the capture shmem and outlive this
                 * call, which is the same contract xrdp has */
                n = vmsplice(datafd[1], &iov, 1, SPLICE_F_NONBLOCK);
            }
            else
            {
                n = write(datafd[1], src + off, (size_t)(nbytes - off));
            }
            if (n > 0)
            {
                off += n;
            }
            else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            {
                struct pollfd pfd;

                pfd.fd = datafd[1];
                pfd.events = POLLOUT;
                pfd.revents = 0;
                poll(&pfd, 1, 1000);
            }
            else if (n < 0 && errno != EINTR)
            {
                close(datafd[1]);
                waitpid(pid, NULL, 0);
                return -1.0;
            }
        }
        /* the transfer is not finished until the CHILD has the bytes */
        if (read(ackfd[0], &ack, 1) != 1)
        {
            close(datafd[1]);
            waitpid(pid, NULL, 0);
            return -1.0;
        }
        t1 = now_s();
        total += t1 - t0;
    }
    close(datafd[1]);
    close(ackfd[0]);
    waitpid(pid, NULL, 0);

    {
        double gbs = (double)nbytes * iters / total / 1e9;
        double ms = total / iters * 1000.0;

        printf("  %-42s %7.2f GB/s %8.3f ms  pipe %4d KiB  %5lld "
               "round trips%s\n", label, gbs, ms, got_sz / 1024,
               (long long)((nbytes + got_sz - 1) / got_sz),
               resize_failed ? "  <-- RESIZE REFUSED" : "");
        return gbs;
    }
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    long long nbytes = (argc > 1) ? atoll(argv[1]) : DEFAULT_BYTES;
    int iters = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERS;
    unsigned char *src;
    unsigned char *dst;
    double base;
    long long i;

    if (nbytes < 4096 || iters < 1)
    {
        fprintf(stderr, "usage: %s [bytes] [iterations]\n", argv[0]);
        return 1;
    }
    /* page-aligned, like the capture shmem vmsplice is normally given */
    if (posix_memalign((void **)&src, 4096, (size_t)nbytes) != 0 ||
            posix_memalign((void **)&dst, 4096, (size_t)nbytes) != 0)
    {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    /* touch every page: a fresh mapping would otherwise fault on first
     * use inside the measured region and be charged to the mechanism */
    for (i = 0; i < nbytes; i++)
    {
        src[i] = (unsigned char)(i & 0xff);
        dst[i] = 0;
    }

    printf("payload %lld bytes (%.2f MB), %d iterations, uid %d\n",
           nbytes, (double)nbytes / 1e6, iters, (int)getuid());
    printf("A pipe resize refused with EPERM means this uid is over\n"
           "fs/pipe-user-pages-soft; the limit is PER USER and host "
           "wide, so a busy\nbox can push every new pipe down to the "
           "kernel minimum. xrdp asks for\n1 MiB and does not check "
           "the answer.\n");
    printf("\n");

    printf("BASELINE -- what one thread on this box can copy\n");
    {
        /* volatile sink: without reading the destination the compiler
         * deletes the whole loop, and the first version of this bench
         * duly printed 5 937 362 GB/s */
        static volatile unsigned char sink;
        double t0 = now_s();
        int k;

        for (k = 0; k < iters; k++)
        {
            memcpy(dst, src, (size_t)nbytes);
            sink = dst[(size_t)(k * 4099) % (size_t)nbytes];
        }
        base = (double)nbytes * iters / (now_s() - t0) / 1e9;
        (void)sink;
        printf("  %-42s %7.2f GB/s %8.3f ms\n",
               "memcpy, same buffer, same size", base,
               (double)nbytes / base / 1e6);
    }
    printf("\n");

    printf("THE SHIPPED MECHANISM -- vmsplice into a 1 MiB pipe, swept "
           "over what the\nREADER asks for per read() call. xrdp's side "
           "is identical in every row;\nonly the child changes.\n");
    run_arm("vmsplice, reader takes 32 KiB at a time", src, nbytes,
            iters, 1024 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, reader takes 64 KiB at a time", src, nbytes,
            iters, 1024 * 1024, 64 * 1024, 1);
    run_arm("vmsplice, reader takes 256 KiB at a time", src, nbytes,
            iters, 1024 * 1024, 256 * 1024, 1);
    run_arm("vmsplice, reader takes 1 MiB at a time", src, nbytes,
            iters, 1024 * 1024, 1024 * 1024, 1);
    printf("\n");

    printf("DOES THE PIPE SIZE MATTER? Same reader chunk (32 KiB), "
           "different pipes.\n");
    run_arm("vmsplice, default pipe (64 KiB)", src, nbytes, iters,
            0, 32 * 1024, 1);
    run_arm("vmsplice, 256 KiB pipe", src, nbytes, iters,
            256 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 1 MiB pipe (what xrdp asks for)", src, nbytes,
            iters, 1024 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 8 MiB pipe requested", src, nbytes, iters,
            8 * 1024 * 1024, 32 * 1024, 1);
    printf("\n");

    printf("WHERE IS THE KNEE? The block above jumps from 64 KiB to "
           "1 MiB and shows no\ndifference, while the 8 KiB a clamped "
           "container gives costs 10x. So the cost is\nNOT the number "
           "of round trips -- it is whether the pipe holds enough for "
           "the\nwriter and the reader to run at the same time instead "
           "of taking turns. This\nsweep is what says where that "
           "starts, and therefore what an ASSURED minimum\npipe size "
           "has to be. Reader chunk is 32 KiB throughout.\n");
    run_arm("vmsplice, 8 KiB pipe (kernel minimum)", src, nbytes,
            iters, 8 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 16 KiB pipe", src, nbytes, iters,
            16 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 32 KiB pipe", src, nbytes, iters,
            32 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 64 KiB pipe", src, nbytes, iters,
            64 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 128 KiB pipe", src, nbytes, iters,
            128 * 1024, 32 * 1024, 1);
    run_arm("vmsplice, 512 KiB pipe", src, nbytes, iters,
            512 * 1024, 32 * 1024, 1);
    printf("\n");

    printf("IS THE ZERO-COPY SIDE BUYING ANYTHING? Same pipe and same "
           "reader, but the\nwriter uses an ordinary write() instead of "
           "vmsplice.\n");
    run_arm("write(), 1 MiB pipe, reader 32 KiB", src, nbytes, iters,
            1024 * 1024, 32 * 1024, 0);
    run_arm("write(), 1 MiB pipe, reader 1 MiB", src, nbytes, iters,
            1024 * 1024, 1024 * 1024, 0);
    printf("\n");

    printf("Read every figure against the memcpy baseline of %.2f GB/s: "
           "a handover\nthrough a pipe costs at least one copy, so that "
           "is the ceiling, and the\ngap between it and a row is what "
           "the pipe machinery costs.\n", base);
    free(src);
    free(dst);
    return 0;
}
