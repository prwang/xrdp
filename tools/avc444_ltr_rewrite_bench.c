/*
 * avc444_ltr_rewrite_bench - offline cost of the FR-H264-8 LTR rewrite.
 *
 * BACKLOG #70 raised the question the local A/B could NOT answer: the
 * rewrite runs inside collect_pair, i.e. inside the measured
 * submit->absorb "encode" leg, mixed with the child's own encode wait.
 * The trace cannot separate them, so this bench calls the SHIPPED
 * entry points (xrdp/xrdp_h264_annexb.o is linked, NOT copied -- unlike
 * avc444_pack_bench.c, nothing here can drift from the deployed code)
 * over real Annex-B access units and reports ms per pair and ns per
 * byte.
 *
 * The rewrite's shape per slice NAL is: unescape a bounded prefix to
 * reach the end of the slice header -> bit-copy of that header with the
 * LTR edits -> verbatim memcpy of the child's already-escaped CABAC
 * payload (BACKLOG #75; before it, the whole packet was unescaped and
 * re-escaped one byte at a time). The cost is still linear in the
 * bitstream because the payload is copied, which is what the ns/byte
 * figure is for: multiply it by the bytes/frame a real session actually
 * produces.
 *
 * build (from repo root, after `make`):
 *   gcc -O2 -I xrdp -I common tools/avc444_ltr_rewrite_bench.c \
 *       xrdp/xrdp_h264_annexb.o common/.libs/libcommon.a \
 *       -lpthread -lrt -ldl -o /tmp/ltr_bench
 * run:
 *   /tmp/ltr_bench main_view.h264 aux_view.h264 [iters]
 *
 * The two inputs must be the two CHILD streams (main: self-referencing
 * P chain after an IDR; aux: the same shape), which is what the runner
 * feeds collect_pair.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xrdp_h264_annexb.h"

#define MAX_AUS 4096

struct au
{
    unsigned char *data;
    int len;
};

/*
 * FNV-1a over every rewritten packet, in order. The point of this bench
 * is to make the rewrite cheaper, and a cheaper rewrite is only a
 * rewrite if it emits the same bytes -- so the digest is printed beside
 * the timing and an optimisation is compared against the digest of the
 * build it replaced. CI's golden vectors (tests/xrdp/test_avc444_ltr.c)
 * pin the same property on small hand-built inputs; this pins it on
 * whole 4K pictures out of a real encoder, which is where a
 * size-dependent mistake would hide.
 */
static unsigned long long
fnv1a(unsigned long long h, const unsigned char *p, int n)
{
    int i;

    for (i = 0; i < n; i++)
    {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static unsigned char *
slurp(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (f == NULL)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n);
    if (buf == NULL || fread(buf, 1, n, f) != (size_t)n)
    {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *out_len = (int)n;
    return buf;
}

/* start code at p? returns prefix length (3 or 4) or 0 */
static int
sc_at(const unsigned char *d, int len, int p)
{
    if (p + 3 <= len && d[p] == 0 && d[p + 1] == 0 && d[p + 2] == 1)
    {
        return 3;
    }
    if (p + 4 <= len && d[p] == 0 && d[p + 1] == 0 && d[p + 2] == 0 &&
            d[p + 3] == 1)
    {
        return 4;
    }
    return 0;
}

/*
 * Split a raw Annex-B byte stream into access units. A boundary opens at
 * an AUD (9), SPS (7) or PPS (8), or at a VCL NAL (1/5) once this AU
 * already holds one. This is only the bench's framing: in production the
 * NUT demuxer hands collect_pair one packet per picture already.
 */
static int
split_aus(unsigned char *d, int len, struct au *out, int max)
{
    int p = 0;
    int n = 0;
    int start = -1;
    int have_vcl = 0;

    while (p < len)
    {
        int sc = sc_at(d, len, p);
        int type;
        int boundary;

        if (sc == 0)
        {
            p++;
            continue;
        }
        if (p + sc >= len)
        {
            break;
        }
        type = d[p + sc] & 0x1f;
        boundary = (type == 9 || type == 7 || type == 8 ||
                    ((type == 1 || type == 5) && have_vcl));
        if (start < 0)
        {
            start = p;
            have_vcl = 0;
        }
        else if (boundary && have_vcl)
        {
            if (n >= max)
            {
                return n;
            }
            out[n].data = d + start;
            out[n].len = p - start;
            n++;
            start = p;
            have_vcl = 0;
        }
        if (type == 1 || type == 5)
        {
            have_vcl = 1;
        }
        p += sc;
    }
    if (start >= 0 && have_vcl && n < max)
    {
        out[n].data = d + start;
        out[n].len = len - start;
        n++;
    }
    return n;
}

int
main(int argc, char **argv)
{
    unsigned char *main_raw;
    unsigned char *aux_raw;
    int main_len;
    int aux_len;
    static struct au main_au[MAX_AUS];
    static struct au aux_au[MAX_AUS];
    int n_main;
    int n_aux;
    int pairs;
    int iters = 20;
    int it;
    int i;
    unsigned char *work;
    int work_cap = 0;
    double t0;
    double t_rw = 0.0;
    double t_copy = 0.0;
    long long bytes = 0;
    long long done = 0;
    int failures = 0;
    unsigned long long digest = 14695981039346656037ULL;

    if (argc < 3)
    {
        fprintf(stderr, "usage: %s main.h264 aux.h264 [iters]\n", argv[0]);
        return 2;
    }
    if (argc > 3)
    {
        iters = atoi(argv[3]);
    }
    main_raw = slurp(argv[1], &main_len);
    aux_raw = slurp(argv[2], &aux_len);
    if (main_raw == NULL || aux_raw == NULL)
    {
        fprintf(stderr, "cannot read inputs\n");
        return 2;
    }
    n_main = split_aus(main_raw, main_len, main_au, MAX_AUS);
    n_aux = split_aus(aux_raw, aux_len, aux_au, MAX_AUS);
    pairs = n_main < n_aux ? n_main : n_aux;
    if (pairs < 2)
    {
        fprintf(stderr, "need at least 2 access units per view "
                "(got main %d aux %d)\n", n_main, n_aux);
        return 2;
    }
    for (i = 0; i < pairs; i++)
    {
        bytes += main_au[i].len + aux_au[i].len;
        if (main_au[i].len > work_cap)
        {
            work_cap = main_au[i].len;
        }
        if (aux_au[i].len > work_cap)
        {
            work_cap = aux_au[i].len;
        }
    }
    work_cap = work_cap * 2 + 4096;
    work = (unsigned char *)malloc(work_cap);
    if (work == NULL)
    {
        return 2;
    }
    printf("main AUs %d  aux AUs %d  pairs %d  "
           "mean pair %lld bytes (main+aux)\n",
           n_main, n_aux, pairs, bytes / pairs);

    for (it = 0; it < iters; it++)
    {
        struct xrdp_h264_ltr_state st;
        memset(&st, 0, sizeof(st));
        /* 0 = "no schedule declared": the observed-vs-requested check
         * (FR-H264-6) is not what this bench is measuring, and a raw
         * ffmpeg stream has no forced schedule to honour */
        st.refresh_period = 0;
        for (i = 0; i < pairs; i++)
        {
            int len;
            int budget;
            int rv;

            /* MAIN: the runner rewrites the child's packet in place in
             * self->main_buf, so the copy in is part of the real cost
             * and is timed separately rather than hidden */
            t0 = now_ms();
            memcpy(work, main_au[i].data, main_au[i].len);
            t_copy += now_ms() - t0;
            len = main_au[i].len;
            budget = xrdp_h264_ltr_growth_budget(work, len);
            if (len + budget > work_cap)
            {
                failures++;
                continue;
            }
            t0 = now_ms();
            rv = xrdp_h264_ltr_rewrite_main(work, &len, work_cap, &st);
            t_rw += now_ms() - t0;
            if (rv != 0)
            {
                failures++;
                continue;
            }
            if (it == 0)
            {
                digest = fnv1a(digest, work, len);
            }
            done++;

            /* AUX */
            t0 = now_ms();
            memcpy(work, aux_au[i].data, aux_au[i].len);
            t_copy += now_ms() - t0;
            len = aux_au[i].len;
            budget = xrdp_h264_ltr_growth_budget(work, len);
            if (len + budget > work_cap)
            {
                failures++;
                continue;
            }
            t0 = now_ms();
            rv = xrdp_h264_ltr_rewrite_aux(work, &len, work_cap, &st);
            t_rw += now_ms() - t0;
            if (rv != 0)
            {
                failures++;
                continue;
            }
            if (it == 0)
            {
                digest = fnv1a(digest, work, len);
            }
            done++;
        }
        /* the rewrite holds a picture-sized scratch buffer on the state
         * (#75); a bench that forgot to release it would grow by one
         * per iteration */
        xrdp_h264_ltr_state_free(&st);
    }

    printf("iters %d  packets rewritten %lld  failures %d\n",
           iters, done, failures);
    printf("output digest (FNV-1a over every rewritten packet) %016llx\n",
           digest);
    printf("rewrite  total %8.1f ms   per PAIR %6.3f ms   %5.2f ns/byte\n",
           t_rw, t_rw / (iters * (double)pairs),
           t_rw * 1.0e6 / (double)(bytes * iters));
    printf("copy-in  total %8.1f ms   per PAIR %6.3f ms   %5.2f ns/byte\n",
           t_copy, t_copy / (iters * (double)pairs),
           t_copy * 1.0e6 / (double)(bytes * iters));
    if (failures > 0)
    {
        printf("NOTE: %d packets were REFUSED by the rewriter; their cost "
               "is in the total but they did not complete -- do not quote "
               "the per-pair figure without explaining this\n", failures);
    }
    return failures > 0 ? 1 : 0;
}
