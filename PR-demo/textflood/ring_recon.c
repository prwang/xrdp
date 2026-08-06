/*
 * ring_recon.c — BACKLOG #65 step 1, reused by BACKLOG #83: what does
 * one frame of each candidate producer design cost, offline (no X
 * server, no RDP session, nothing deployed is touched — pure compute)?
 *
 *   A  full-frame live render      what textflood's --scroll full does:
 *                                  cairo renders every visible row,
 *                                  every frame.
 *   B  scroll + strip render       textflood's --scroll strip: memmove
 *                                  the frame up by the step, cairo
 *                                  renders only the newly exposed
 *                                  bottom strip. Keeps per-frame CPU
 *                                  text rendering (FR-BENCH-1 req 2 in
 *                                  its strictest reading) and still
 *                                  damages the whole frame every frame
 *                                  (every pixel moves).
 *   C  pre-rendered frame ring     steady state is ONE memcpy of the
 *                                  frame (ring slot -> blit source).
 *                                  Render cost is paid once at startup.
 *
 * The X server's own XShmPutImage cost is approximately one more
 * frame-sized memcpy on the X thread; bench C's copy rate bounds that
 * too. The 2-thread variant of C estimates the memory-bandwidth
 * ceiling with the pipeline's own copies running concurrently — on 2
 * physical cores every producer byte competes with the capture pack
 * and the vmsplice feed for the same DRAM.
 *
 * WHY THIS BENCH AND NOT textflood --selftest. Both are wanted and they
 * answer different questions. --selftest lets the producer free-run, so
 * its time-based content advance settles at a couple of corpus lines
 * per frame and it reports the standalone frame rate FR-BENCH-1 asks
 * for. This bench pins the advance at DEF_STEP lines for BOTH designs,
 * which is the only way A and B are comparable: A's cost does not
 * depend on the advance and B's is nearly proportional to it, so a
 * comparison at different advances is not a comparison.
 *
 * FIXED 2026-08-06 (BACKLOG #83): bench B rendered the newly exposed
 * strip from THE WRONG PLACE IN THE CORPUS. It called the row renderer
 * with a corpus offset of `offset + rows - strip_rows` while asking for
 * screen rows [rows - strip_rows, rows), and the renderer adds the row
 * index to the offset — so the strip showed corpus line
 * offset + (rows - strip_rows) + row instead of offset + step + row,
 * i.e. rows - 2*step lines off — 85 lines at 3840x2160, where the
 * bench computed 135 rows of 16 px. Bench B's
 * TIMING was unaffected — the same number of rows of the same corpus
 * were rendered either way — but the picture it drew was never the
 * picture the design produces, so the bench could never have shown a
 * strip-geometry error. The strip path is now the one textflood.c
 * ships, including its band clipping, and textflood --verify is what
 * proves that path pixel-exact against a full redraw.
 *
 * The corpus structs, parser, loader and the row-render loop are
 * VERBATIM copies of PR-demo/textflood/textflood.c (keep in sync).
 *
 * Build:  cc -O2 -Wall -Wextra -o ring_recon ring_recon.c \
 *             $(pkg-config --cflags --libs cairo) -lpthread
 * Run:    ./ring_recon [corpus] [WxH]        default 3840x2160
 */

#include <cairo/cairo.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEF_WIDTH 3840
#define DEF_HEIGHT 2160
#define DEF_CORPUS "/usr/local/share/code_corpus.ansi"
#define DEF_FONT "DejaVu Sans Mono"
#define DEF_FONT_SIZE 14.0
#define DEF_STEP 25

/* FR-BENCH-1: the producer must be >= 2x the pipeline's measured rate.
   Reference period 18.476 ms = 54.1 sends/s, one monitor at 3840x2400,
   textflood, arm x014 (BACKLOG #75, capture i75_x014_rewrite_20260801).
   This replaces the T4-era 8.19 sends/s of the 2026-07-31 revision,
   which was measured at 3840x2160 on hardware that no longer exists —
   quoting a PASS against it would be quoting a dead pipeline. */
#define PIPELINE_MS 18.476
#define PIPELINE_FPS (1000.0 / PIPELINE_MS)
#define REQUIRED_FPS (2.0 * PIPELINE_FPS)

#define BG_R 0.0000
#define BG_G 0.1686
#define BG_B 0.2118
#define FG_R 131
#define FG_G 148
#define FG_B 150

/* ---- verbatim from textflood.c: corpus ---------------------------------- */

struct run
{
    unsigned char r;
    unsigned char g;
    unsigned char b;
    int len;
    char *text;
};

struct line
{
    struct run *runs;
    int nruns;
};

struct corpus
{
    struct line *lines;
    int nlines;
};

static int
line_add_run(struct line *ln, const char *text, int len,
             unsigned char r, unsigned char g, unsigned char b)
{
    struct run *nr;

    if (len <= 0)
    {
        return 0;
    }
    nr = (struct run *) realloc(ln->runs, (ln->nruns + 1) * sizeof(*nr));
    if (nr == NULL)
    {
        return 1;
    }
    ln->runs = nr;
    nr = ln->runs + ln->nruns;
    nr->text = (char *) malloc(len + 1);
    if (nr->text == NULL)
    {
        return 1;
    }
    memcpy(nr->text, text, len);
    nr->text[len] = 0;
    nr->len = len;
    nr->r = r;
    nr->g = g;
    nr->b = b;
    ln->nruns++;
    return 0;
}

static int
parse_line(struct line *ln, const char *s, int slen)
{
    int i;
    int start;
    unsigned char cr;
    unsigned char cg;
    unsigned char cb;

    cr = FG_R;
    cg = FG_G;
    cb = FG_B;
    i = 0;
    start = 0;
    while (i < slen)
    {
        if (s[i] != 0x1b || i + 1 >= slen || s[i + 1] != '[')
        {
            i++;
            continue;
        }
        if (line_add_run(ln, s + start, i - start, cr, cg, cb) != 0)
        {
            return 1;
        }
        i += 2;
        {
            int p[8];
            int np;
            int v;

            np = 0;
            v = 0;
            while (i < slen && s[i] != 'm' && s[i] >= ' ')
            {
                if (s[i] >= '0' && s[i] <= '9')
                {
                    v = v * 10 + (s[i] - '0');
                }
                else if (s[i] == ';')
                {
                    if (np < 8)
                    {
                        p[np++] = v;
                    }
                    v = 0;
                }
                i++;
            }
            if (np < 8)
            {
                p[np++] = v;
            }
            if (i < slen && s[i] == 'm')
            {
                i++;
            }
            if (np >= 5 && p[0] == 38 && p[1] == 2)
            {
                cr = (unsigned char) p[2];
                cg = (unsigned char) p[3];
                cb = (unsigned char) p[4];
            }
            else if (np >= 1 && p[0] == 0)
            {
                cr = FG_R;
                cg = FG_G;
                cb = FG_B;
            }
        }
        start = i;
    }
    return line_add_run(ln, s + start, slen - start, cr, cg, cb);
}

static int
corpus_load(struct corpus *cp, const char *path)
{
    FILE *fp;
    char *buf;
    long size;
    long i;
    long start;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "ring_recon: cannot open corpus %s\n", path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0)
    {
        fclose(fp);
        return 1;
    }
    buf = (char *) malloc(size);
    if (buf == NULL)
    {
        fclose(fp);
        return 1;
    }
    got = fread(buf, 1, size, fp);
    fclose(fp);
    if ((long) got != size)
    {
        free(buf);
        return 1;
    }
    cp->lines = NULL;
    cp->nlines = 0;
    start = 0;
    for (i = 0; i <= size; i++)
    {
        if (i == size || buf[i] == '\n')
        {
            struct line *nl;

            nl = (struct line *) realloc(cp->lines,
                                         (cp->nlines + 1) * sizeof(*nl));
            if (nl == NULL)
            {
                free(buf);
                return 1;
            }
            cp->lines = nl;
            nl = cp->lines + cp->nlines;
            nl->runs = NULL;
            nl->nruns = 0;
            if (parse_line(nl, buf + start, (int) (i - start)) != 0)
            {
                free(buf);
                return 1;
            }
            cp->nlines++;
            start = i + 1;
        }
    }
    free(buf);
    return cp->nlines <= 0;
}

/* ---- textflood.c row renderer, verbatim --------------------------------- */

static void
draw_row_text(cairo_t *cr, const struct line *ln, int width, double y)
{
    int ri;
    double x;
    cairo_text_extents_t ext;

    x = 0.0;
    while (x < width)
    {
        double x_before;

        x_before = x;
        for (ri = 0; ri < ln->nruns && x < width; ri++)
        {
            const struct run *rn;

            rn = ln->runs + ri;
            cairo_set_source_rgb(cr, rn->r / 255.0, rn->g / 255.0,
                                 rn->b / 255.0);
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, rn->text);
            cairo_text_extents(cr, rn->text, &ext);
            x += ext.x_advance;
        }
        if (x <= x_before)
        {
            break;
        }
        x += 8.0;
    }
}

/* bench A's frame: textflood.c draw_frame, verbatim */
static void
draw_frame(cairo_t *cr, const struct corpus *cp, int offset,
           int width, int rows, double line_height, double baseline)
{
    int row;
    int li;

    cairo_set_source_rgb(cr, BG_R, BG_G, BG_B);
    cairo_paint(cr);
    for (row = 0; row < rows; row++)
    {
        const struct line *ln;

        li = (offset + row) % cp->nlines;
        ln = cp->lines + li;
        if (ln->nruns == 0)
        {
            continue;
        }
        draw_row_text(cr, ln, width, row * line_height + baseline);
    }
}

/* bench B's rows: textflood.c draw_rows_banded, verbatim apart from
   taking the layout as loose arguments rather than a struct */
static void
draw_rows_banded(cairo_t *cr, const struct corpus *cp, int offset,
                 int width, int row_from, int row_to, int line_px,
                 double baseline)
{
    int row;
    int li;

    for (row = row_from; row < row_to; row++)
    {
        const struct line *ln;

        li = (offset + row) % cp->nlines;
        ln = cp->lines + li;
        if (ln->nruns == 0)
        {
            continue;
        }
        cairo_save(cr);
        cairo_rectangle(cr, 0, row * line_px, width, line_px);
        cairo_clip(cr);
        draw_row_text(cr, ln, width, row * line_px + baseline);
        cairo_restore(cr);
    }
}

/* ---- timing ------------------------------------------------------------- */

static double
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void
report(const char *name, double ms, const char *note)
{
    double fps = 1000.0 / ms;

    printf("  %-28s %7.2f ms/frame = %6.1f fps  %5.2fx pipeline  %-4s %s\n",
           name, ms, fps, PIPELINE_MS / ms,
           fps >= REQUIRED_FPS ? "PASS" : "FAIL", note);
}

/* ---- bench C: frame-sized memcpy, 1..2 threads -------------------------- */

struct copy_arg
{
    unsigned char *dst;
    const unsigned char *src;
    size_t bytes;
    int iters;
};

static void *
copy_thread(void *p)
{
    struct copy_arg *a = (struct copy_arg *) p;
    int i;

    for (i = 0; i < a->iters; i++)
    {
        memcpy(a->dst, a->src, a->bytes);
    }
    return NULL;
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    const char *corpus_path;
    struct corpus cp;
    cairo_surface_t *surf;
    cairo_t *cr;
    cairo_font_options_t *fo;
    cairo_font_extents_t fext;
    unsigned char *data;
    size_t frame_bytes;
    double line_height;
    double t0;
    double ms_full;
    double ms_strip;
    double ms_copy1;
    double ms_copy2;
    int width;
    int height;
    int stride;
    int rows;
    int line_px;
    int rows_banded;
    int rb;
    int shift;
    int offset;
    int i;
    int n;

    corpus_path = argc > 1 ? argv[1] : DEF_CORPUS;
    width = DEF_WIDTH;
    height = DEF_HEIGHT;
    if (argc > 2 && sscanf(argv[2], "%dx%d", &width, &height) != 2)
    {
        fprintf(stderr, "ring_recon: geometry wants WxH, not %s\n", argv[2]);
        return 1;
    }
    if (width < 64 || height < 64 || width > 16384 || height > 16384)
    {
        fprintf(stderr, "ring_recon: geometry out of range\n");
        return 1;
    }
    if (corpus_load(&cp, corpus_path) != 0)
    {
        return 1;
    }

    surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cr = cairo_create(surf);
    /* identical AA setup to textflood.c */
    fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
    cairo_font_options_set_subpixel_order(fo, CAIRO_SUBPIXEL_ORDER_RGB);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
    cairo_set_font_options(cr, fo);
    cairo_select_font_face(cr, DEF_FONT, CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, DEF_FONT_SIZE);
    cairo_font_extents(cr, &fext);
    line_height = fext.height;
    /* rows as the shipped full-redraw loop computes it: height/lh + 1,
       so the partially visible bottom row is drawn. The 2026-07-31
       revision of this bench used height/lh and so rendered one row
       fewer than the payload it was standing in for. */
    rows = (int) (height / line_height) + 1;
    /* strip mode's pinned integer line height, exactly as textflood.c's
       layout_init() computes it */
    line_px = (int) (line_height + 0.5);
    rows_banded = height / line_px + 1;
    shift = DEF_STEP * line_px;
    rb = rows_banded - 1 - DEF_STEP;
    data = cairo_image_surface_get_data(surf);
    stride = cairo_image_surface_get_stride(surf);
    frame_bytes = (size_t) stride * height;
    if (rb < 1 || shift >= height)
    {
        fprintf(stderr, "ring_recon: %d lines does not scroll a %d px "
                "frame of %d px bands\n", DEF_STEP, height, line_px);
        return 1;
    }

    printf("ring_recon: %dx%d, %d rows of %.3f px (strip mode: %d bands "
           "of %d px), corpus %d lines, step %d lines, frame %.1f MB\n",
           width, height, rows, line_height, rows_banded, line_px,
           cp.nlines, DEF_STEP, frame_bytes / 1048576.0);
    printf("FR-BENCH-1 floor: 2 x %.1f pipeline fps = %.1f fps "
           "(<= %.2f ms/frame); pipeline period %.3f ms\n\n",
           PIPELINE_FPS, REQUIRED_FPS, 1000.0 / REQUIRED_FPS, PIPELINE_MS);

    /* ---- A: full-frame live render (textflood --scroll full) ---- */
    offset = 0;
    n = 12;
    for (i = 0; i < 2; i++)   /* warm the glyph cache */
    {
        draw_frame(cr, &cp, offset, width, rows, line_height, fext.ascent);
        offset += DEF_STEP;
    }
    t0 = now_ms();
    for (i = 0; i < n; i++)
    {
        draw_frame(cr, &cp, offset, width, rows, line_height, fext.ascent);
        cairo_surface_flush(surf);
        offset += DEF_STEP;
    }
    ms_full = (now_ms() - t0) / n;
    report("A full-frame live render", ms_full, "(--scroll full)");

    /* ---- B: memmove scroll + strip render (textflood --scroll strip),
       at the SAME DEF_STEP lines per frame as A ---- */
    n = 40;
    t0 = now_ms();
    for (i = 0; i < n; i++)
    {
        /* offset is the corpus line shown by band 0 AFTER this scroll,
           so the exposed bands must be drawn from it too — the bug
           fixed 2026-08-06 passed a different base here */
        offset += DEF_STEP;
        cairo_surface_flush(surf);
        memmove(data, data + (size_t) shift * stride,
                (size_t) stride * (height - shift));
        cairo_surface_mark_dirty(surf);
        cairo_save(cr);
        cairo_rectangle(cr, 0, rb * line_px, width, height - rb * line_px);
        cairo_clip(cr);
        cairo_set_source_rgb(cr, BG_R, BG_G, BG_B);
        cairo_paint(cr);
        cairo_restore(cr);
        draw_rows_banded(cr, &cp, offset, width, rb, rows_banded, line_px,
                         fext.ascent);
        cairo_surface_flush(surf);
    }
    ms_strip = (now_ms() - t0) / n;
    report("B scroll + strip render", ms_strip,
           "(--scroll strip, live CPU text render kept)");

    /* ---- C: pre-rendered ring steady state = one frame memcpy ---- */
    {
        unsigned char *ring;
        unsigned char *shm;
        struct copy_arg a[2];
        pthread_t th;

        ring = (unsigned char *) malloc(frame_bytes * 2);
        shm = (unsigned char *) malloc(frame_bytes * 2);
        if (ring == NULL || shm == NULL)
        {
            return 1;
        }
        memcpy(ring, data, frame_bytes);
        memcpy(ring + frame_bytes, data, frame_bytes);

        n = 60;
        t0 = now_ms();
        for (i = 0; i < n; i++)
        {
            memcpy(shm, ring + (i & 1) * frame_bytes, frame_bytes);
        }
        ms_copy1 = (now_ms() - t0) / n;
        report("C ring blit, 1 thread", ms_copy1,
               "(pre-rendered ring steady state)");

        /* two concurrent frame copies: the bandwidth ceiling the
           producer shares with the capture pack + vmsplice feed */
        a[0].dst = shm;
        a[0].src = ring;
        a[0].bytes = frame_bytes;
        a[0].iters = 30;
        a[1].dst = shm + frame_bytes;
        a[1].src = ring + frame_bytes;
        a[1].bytes = frame_bytes;
        a[1].iters = 30;
        t0 = now_ms();
        pthread_create(&th, NULL, copy_thread, &a[1]);
        copy_thread(&a[0]);
        pthread_join(th, NULL);
        ms_copy2 = (now_ms() - t0) / 30;
        printf("  %-28s %7.2f ms per PAIR of frames = %.1f GB/s "
               "aggregate\n", "C ring blit, 2 threads", ms_copy2,
               2.0 * frame_bytes / 1048576.0 / 1024.0
               / (ms_copy2 / 1000.0));
        free(ring);
        free(shm);
    }

    printf("\nVERDICT (producer alone; X-side blit adds ~1 more frame "
           "copy on the X thread):\n");
    printf("  A %6.1f fps  B %6.1f fps  C %6.1f fps  vs floor %.1f fps\n",
           1000.0 / ms_full, 1000.0 / ms_strip, 1000.0 / ms_copy1,
           REQUIRED_FPS);
    printf("  B is %.2fx cheaper per frame than A at the same %d lines "
           "of scroll\n", ms_full / ms_strip, DEF_STEP);

    cairo_font_options_destroy(fo);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}
