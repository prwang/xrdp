/**
 * textflood — a client-side-rendered scrolling code payload for the E5-2
 * frame-interval benchmark.
 *
 * WHY THIS EXISTS
 *
 * The E5-2 benchmark used an xterm scrolling an ANSI-highlighted corpus.
 * Profiling the T4 (BACKLOG #59) showed that payload spends 44.9 % of the
 * session Xorg's single core drawing ITSELF — glyphs through
 * pixman_image_composite32 (19.3 %), scroll through pixman_blt (16.5 %),
 * fills through fbFill (9.1 %) — against 13.8 % for the whole xrdp
 * capture. A benchmark in which two thirds of the bottleneck thread is
 * the payload's own software rendering measures the X server, not the
 * pipeline we own, so it cannot quantify a change to our pipeline.
 *
 * xterm draws server-side: every glyph is an XRender request that the X
 * server rasterizes on the same thread that later runs our capture.
 * textflood rasterizes in ITS OWN process with cairo and hands the X
 * server one finished image per frame over MIT-SHM. The X thread's share
 * of the payload drops to a single memcpy, leaving the capture and pack
 * as the dominant cost — which is the ceiling we own and want to measure.
 *
 * WHY NOT A GPU TERMINAL (alacritty, kitty)
 *
 * They render through OpenGL. On this box there is no usable GL: xorgxrdp
 * calls glamor_init() with GLAMOR_NO_DRI3, so clients fall back to Mesa
 * zink/llvmpipe, which EMULATES the GL state machine on the CPU and would
 * simply become the new bottleneck. Enabling GLAMOR to fix that renders
 * black on NVIDIA (upstream neutrinolabs/xrdp#1697, BACKLOG #61). cairo's
 * image backend is a native CPU rasterizer — the same FreeType glyph
 * rasterizer GTK applications use — so it is fast on the CPU by design
 * rather than by emulation.
 *
 * WHY THE CONTENT IS UNCHANGED
 *
 * A benchmark payload has to stay representative of real use, so this
 * renders the SAME corpus as the xterm payload: code_corpus.ansi, real
 * xrdp source highlighted by pygments + clangd in solarized-dark
 * (gen_code_corpus.py). Only WHERE the pixels are rasterized changes, so
 * numbers stay comparable with the codeflood history.
 *
 * Subpixel antialiasing is requested explicitly. It is what a real
 * desktop renders, and it is also the maximal stressor for AVC444: it
 * puts a different colour in each of R, G and B at every glyph edge, and
 * that per-pixel chroma detail is exactly what 4:2:0 destroys and the
 * 4:4:4 aux view preserves.
 *
 * THE CORPUS DIALECT
 *
 * code_corpus.ansi uses exactly two escape sequences — ESC[38;2;R;G;Bm
 * (24-bit truecolor foreground) and ESC[0m (reset) — over 8 solarized
 * colours. This is not a terminal emulator and does not try to be: any
 * other escape is skipped. Regenerating the corpus with a style that
 * emits bold, backgrounds or 256-colour indices would silently render
 * differently, so gen_code_corpus.py and this parser are a pair.
 *
 * THE TWO SCROLL MODES (BACKLOG #83)
 *
 * --scroll full (DEFAULT) is the historical loop, unchanged: every
 * visible row is re-rendered every frame and the corpus advances --step
 * lines PER RENDERED FRAME. Every archived capture was measured with
 * this loop, so it must keep producing the same pixels; nothing in the
 * strip path runs when it is selected.
 *
 * --scroll strip is PRD FR-BENCH-1's design B: the surface is moved up
 * by the scroll distance with one memmove and only the newly exposed
 * bottom strip is rendered. Two things change with it, both deliberate:
 *
 *   1. Rows are drawn CLIPPED TO THEIR OWN BAND. A band is line_height
 *      pixels tall, band r is [r*lh, (r+1)*lh), and row r's glyphs are
 *      masked to band r. That makes "scroll by k bands + redraw the
 *      exposed bands" pixel-exact against "redraw every band", which is
 *      what --verify checks. Without the clip, a descender crossing a
 *      band boundary would survive the scroll at the top of the frame
 *      and be clipped away at the bottom, and the two paths would
 *      differ — not by much, but not provably identical either.
 *   2. The corpus advance is TIME-BASED, not per-frame. A producer that
 *      draws 2.4x faster must not also put 2.4x more motion into every
 *      encoded frame, or "the producer got faster" silently means "the
 *      encoder's job got harder" and the ratio #83 exists to clean up is
 *      confounded again. The rate is set so the deployed content speed
 *      is unchanged: the full-redraw payload advanced DEF_STEP = 25
 *      corpus lines per frame at a measured 16.901 ms frame interval
 *      (arm x014, 3840x2400, capture i75_x014_rewrite_20260801), i.e.
 *      25 / 0.016901 s = 1479.2 lines/s — DEF_LINES_PER_SEC below.
 *
 * The line height is pinned to an INTEGER number of pixels in strip
 * mode and checked at startup, because the scroll distance must be an
 * exact multiple of it for the strip render to be pixel-exact. The
 * natural DejaVu Sans Mono 14px extent is fractional, so strip mode's
 * row spacing differs from the archived full-redraw captures by the
 * rounding — see README.md, it is not hidden.
 *
 * OFFLINE MODES (no X server, no session, seconds of CPU)
 *
 *   --verify N    render N frames down BOTH paths into two surfaces and
 *                 compare them byte for byte; exit non-zero on the first
 *                 mismatch. The expected pixels come from the OTHER code
 *                 path, which is why this is a test and not a tautology.
 *   --selftest    FR-BENCH-1's required producer telemetry: the payload's
 *                 own standalone frame rate with no RDP session, printed
 *                 beside the pipeline period it has to beat by 2x.
 *
 * Build: PR-demo/textflood/build.sh   (cairo + X11 + Xext + RandR,
 *                                      no toolkit)
 * Usage: textflood [options]; -h for the list.
 */

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xrandr.h>
#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define DEF_CORPUS "/usr/local/share/code_corpus.ansi"
/* FR-BENCH-1: the producer must log its own frame timestamps so a gate
   run can verify saturation instead of assuming it. Default ON; the
   file is one short line per frame (~8/s), overridable with --stamps. */
#define DEF_STAMPS "/tmp/e52_textflood_stamps.tsv"

struct monitor_geometry
{
    int x;
    int y;
    int width;
    int height;
};

static int
monitor_geometry_compare(const void *a, const void *b)
{
    const struct monitor_geometry *ma = a;
    const struct monitor_geometry *mb = b;

    if (ma->x != mb->x)
    {
        return ma->x < mb->x ? -1 : 1;
    }
    if (ma->y != mb->y)
    {
        return ma->y < mb->y ? -1 : 1;
    }
    return 0;
}

/* Xorgxrdp exposes one Xinerama screen covering the whole desktop, so
   Xinerama cannot select a client monitor. Its RandR outputs retain the
   client rectangles. Sort those rectangles into deterministic desktop order
   and ignore cloned outputs which name the same rectangle. */
static int
query_monitor_geometry(Display *dpy, Window root, int monitor,
                       struct monitor_geometry *selected)
{
    struct monitor_geometry geometries[16];
    XRRScreenResources *resources;
    int count;
    int oi;

    count = 0;
    resources = XRRGetScreenResourcesCurrent(dpy, root);
    if (resources == NULL)
    {
        return 0;
    }
    for (oi = 0; oi < resources->noutput && count < 16; oi++)
    {
        XRROutputInfo *output;
        XRRCrtcInfo *crtc;
        int duplicate;
        int gi;

        output = XRRGetOutputInfo(dpy, resources, resources->outputs[oi]);
        if (output == NULL || output->connection != RR_Connected
                || output->crtc == None)
        {
            if (output != NULL)
            {
                XRRFreeOutputInfo(output);
            }
            continue;
        }
        crtc = XRRGetCrtcInfo(dpy, resources, output->crtc);
        XRRFreeOutputInfo(output);
        if (crtc == NULL || crtc->width == 0 || crtc->height == 0)
        {
            if (crtc != NULL)
            {
                XRRFreeCrtcInfo(crtc);
            }
            continue;
        }
        duplicate = 0;
        for (gi = 0; gi < count; gi++)
        {
            duplicate = geometries[gi].x == crtc->x
                        && geometries[gi].y == crtc->y
                        && geometries[gi].width == (int) crtc->width
                        && geometries[gi].height == (int) crtc->height;
            if (duplicate)
            {
                break;
            }
        }
        if (!duplicate)
        {
            geometries[count].x = crtc->x;
            geometries[count].y = crtc->y;
            geometries[count].width = crtc->width;
            geometries[count].height = crtc->height;
            count++;
        }
        XRRFreeCrtcInfo(crtc);
    }
    XRRFreeScreenResources(resources);
    qsort(geometries, count, sizeof(geometries[0]),
          monitor_geometry_compare);
    if (monitor >= 0 && monitor < count)
    {
        *selected = geometries[monitor];
    }
    return count;
}
#define DEF_TITLE "E52FLOOD"
#define DEF_FONT "DejaVu Sans Mono"
#define DEF_FONT_SIZE 14.0
#define DEF_STEP 25

/* Strip mode's content speed, in corpus lines per second of wall clock.
   25 lines/frame / 0.016901 s/frame = 1479.2 lines/s — the speed the
   full-redraw payload actually ran at on arm x014 (3840x2400, capture
   i75_x014_rewrite_20260801, producer frame interval 16.901 ms). Held
   fixed so a faster producer emits more frames, not more motion. */
#define DEF_LINES_PER_SEC 1479.2

/* geometry used by the offline modes, which have no root window to ask.
   3840x2400 is the geometry the fleet arms are measured at. */
#define DEF_OFF_W 3840
#define DEF_OFF_H 2400

/* the pipeline frame period the printed FR-BENCH-1 margin is taken
   against: 18.476 ms, arm x014 at 3840x2400 (BACKLOG #75). Override
   with --pipeline-ms when comparing against a different arm. */
#define DEF_PIPELINE_MS 18.476

#define MODE_FULL 0
#define MODE_STRIP 1

/* solarized base03: the background xterm was launched with, kept so the
   two payloads encode the same picture */
#define BG_R 0.0000
#define BG_G 0.1686
#define BG_B 0.2118

/* solarized base0: the corpus' implicit colour after ESC[0m */
#define FG_R 131
#define FG_G 148
#define FG_B 150

/* a coloured span of one corpus line */
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

/* everything the strip path needs to know about the frame's layout.
   Filled once by layout_init() and never changed afterwards: the scroll
   arithmetic is only exact while the line height is a fixed integer. */
struct layout
{
    int width;
    int height;
    int line_px;
    int rows;
    double baseline;
};

/*****************************************************************************/
static double
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/*****************************************************************************/
/* Append one run to a line. Runs are short and lines few, so a realloc
   per run is cheaper than the bookkeeping to avoid it, and this happens
   once at startup rather than per frame. */
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

/*****************************************************************************/
/* Parse one corpus line into coloured runs. Understands ESC[38;2;R;G;Bm
   and ESC[0m; any other CSI sequence is consumed and ignored so a corpus
   refresh that adds one degrades to a colour change rather than to
   garbage on screen. */
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

/*****************************************************************************/
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
        fprintf(stderr, "textflood: cannot open corpus %s\n", path);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0)
    {
        fprintf(stderr, "textflood: corpus %s is empty\n", path);
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
    if (cp->nlines <= 0)
    {
        fprintf(stderr, "textflood: corpus %s has no lines\n", path);
        return 1;
    }
    return 0;
}

/*****************************************************************************/
/* Draw one row of text: corpus line (offset + row), repeated across the
   full width. The xterm payload got the same effect by repeating each
   line 32 times and letting the terminal wrap it, and the point is the
   same: no row is mostly background, so a frame is a full-width content
   change rather than a sparse one. */
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
        /* a line of only zero-advance runs would spin forever */
        if (x <= x_before)
        {
            break;
        }
        x += 8.0;
    }
}

/*****************************************************************************/
/* The historical full-redraw frame, unchanged. Every archived capture
   was measured against these exact pixels; --scroll full still runs it
   and nothing else. */
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

/*****************************************************************************/
/* Strip mode's row renderer: rows [row_from, row_to), each masked to its
   own band. The mask is what makes a scrolled band and a freshly drawn
   band the same pixels — see the header note on --scroll strip. */
static void
draw_rows_banded(cairo_t *cr, const struct corpus *cp, int offset,
                 const struct layout *lay, int row_from, int row_to)
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
        cairo_rectangle(cr, 0, row * lay->line_px, lay->width,
                        lay->line_px);
        cairo_clip(cr);
        draw_row_text(cr, ln, lay->width,
                      row * lay->line_px + lay->baseline);
        cairo_restore(cr);
    }
}

/*****************************************************************************/
/* Strip mode's full redraw: background plus every band. Used for frame
   zero, after an expose, and whenever the advance is too large for a
   scroll to leave anything on screen. */
static void
render_full_banded(cairo_t *cr, const struct corpus *cp, int offset,
                   const struct layout *lay)
{
    cairo_set_source_rgb(cr, BG_R, BG_G, BG_B);
    cairo_paint(cr);
    draw_rows_banded(cr, cp, offset, lay, 0, lay->rows);
}

/*****************************************************************************/
/* Strip mode's fast path: move the picture up by `advance` bands and
   render only what that exposed at the bottom.
 *
 * `offset` is the NEW corpus offset, i.e. band r must end up showing
 * corpus line offset + r.
 *
 * Which bands need redrawing: the shift is advance * line_px pixels, so
 * every band boundary lands on a band boundary and band r keeps valid
 * pixels while its whole extent survives the move, i.e. while
 * (r + advance + 1) * line_px <= height. With rows = height / line_px + 1
 * that is r <= rows - 2 - advance. The band at rows - 1 - advance
 * straddles the boundary — its top is valid, its bottom is stale — so
 * the redrawn range is [rows - 1 - advance, rows), advance + 1 bands.
 * Redrawing the straddling band in full is harmless: band clipping makes
 * a redrawn band bit-identical to the scrolled one.
 *
 * Returns 0 when it scrolled, 1 when the advance is too large for a
 * scroll to leave anything on screen and the caller must full-redraw,
 * and -1 when an invariant the exactness rests on is broken. -1 is a
 * bug in this file, never a runtime condition, and callers must treat
 * it as fatal: quietly full-redrawing instead would turn a wrong strip
 * render into a silently slower producer, which is the failure this
 * whole item exists to remove.
 */
static int
render_scroll(cairo_t *cr, cairo_surface_t *surf, const struct corpus *cp,
              int offset, int advance, const struct layout *lay)
{
    unsigned char *data;
    int stride;
    int shift;
    int rb;

    if (advance < 1 || advance > lay->rows - 2)
    {
        return 1;
    }
    shift = advance * lay->line_px;
    if (shift <= 0 || shift >= lay->height)
    {
        return 1;
    }
    /* the invariants the exactness rests on, checked rather than
       assumed: the scroll distance is a whole number of line heights,
       and the first redrawn band is exactly `advance` bands above the
       last one */
    rb = lay->rows - 1 - advance;
    if (shift % lay->line_px != 0 || rb < 0 || rb + advance != lay->rows - 1)
    {
        fprintf(stderr, "textflood: scroll %d px does not tile the %d px "
                "line height (rows %d, advance %d)\n", shift, lay->line_px,
                lay->rows, advance);
        return -1;
    }
    cairo_surface_flush(surf);
    data = cairo_image_surface_get_data(surf);
    stride = cairo_image_surface_get_stride(surf);
    memmove(data, data + (size_t) shift * stride,
            (size_t) stride * (lay->height - shift));
    cairo_surface_mark_dirty(surf);
    cairo_save(cr);
    cairo_rectangle(cr, 0, rb * lay->line_px, lay->width,
                    lay->height - rb * lay->line_px);
    cairo_clip(cr);
    cairo_set_source_rgb(cr, BG_R, BG_G, BG_B);
    cairo_paint(cr);
    cairo_restore(cr);
    draw_rows_banded(cr, cp, offset, lay, rb, lay->rows);
    return 0;
}

/*****************************************************************************/
/* The font setup is identical in the live loop and in both offline
   modes, on purpose: a --verify or --selftest run that rasterized
   differently from the deployed payload would be measuring a different
   program. */
static cairo_font_options_t *
setup_font(cairo_t *cr, const char *font, double font_size,
           cairo_font_extents_t *fext)
{
    cairo_font_options_t *fo;

    /* SUBPIXEL, explicitly. The box already has it system-wide
       (fontconfig rgba=rgb, lcdfilter=default), but the benchmark must
       not depend on a session's fontconfig for what it encodes: a run
       that quietly fell back to greyscale AA would carry much less
       chroma detail and flatter the 4:2:0 arm. */
    fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_SUBPIXEL);
    cairo_font_options_set_subpixel_order(fo, CAIRO_SUBPIXEL_ORDER_RGB);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
    cairo_set_font_options(cr, fo);
    cairo_select_font_face(cr, font, CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, font_size);
    cairo_font_extents(cr, fext);
    return fo;
}

/*****************************************************************************/
/* Pin the strip-mode layout and check it. The line height MUST be a
   whole number of pixels: the scroll distance is advance * line_px and
   a fractional height would put band boundaries between pixels, which
   is the one way the strip render can be subtly wrong instead of
   loudly wrong. Returns non-zero if this geometry cannot be scrolled. */
static int
layout_init(struct layout *lay, int width, int height,
            const cairo_font_extents_t *fext, double font_size)
{
    double natural;

    natural = fext->height;
    if (natural < 1.0)
    {
        natural = font_size;
    }
    lay->width = width;
    lay->height = height;
    lay->line_px = (int) (natural + 0.5);
    lay->baseline = fext->ascent;
    if (lay->line_px < 4)
    {
        fprintf(stderr, "textflood: line height %d px is too small to "
                "scroll (natural %.3f)\n", lay->line_px, natural);
        return 1;
    }
    lay->rows = height / lay->line_px + 1;
    if (lay->rows < 4)
    {
        fprintf(stderr, "textflood: %d rows of %d px is too few to "
                "scroll\n", lay->rows, lay->line_px);
        return 1;
    }
    /* The row count and the scroll distance have to agree, and this is
       where a wrong one is cheap to catch: band rows-1 must be the one
       the bottom edge falls in, so that a scroll of `advance` bands
       leaves bands 0..rows-2-advance untouched. */
    if ((lay->rows - 1) * lay->line_px > height
            || lay->rows * lay->line_px <= height)
    {
        fprintf(stderr, "textflood: %d bands of %d px do not tile %d px "
                "of height\n", lay->rows, lay->line_px, height);
        return 1;
    }
    return 0;
}

/*****************************************************************************/
/* How many corpus lines the content should have advanced by now.
   Time-based, so the picture moves at DEF_LINES_PER_SEC whatever the
   frame rate is; `acc` carries the fractional remainder so the long-run
   rate is exact. Clamped to at least one line — at the rates this
   payload runs (0.676 ms per line) the clamp does not fire, but a frame
   that advanced nothing would present identical pixels. */
static int
advance_lines(double *acc, double dt_ms, double lines_per_sec)
{
    double owed;
    int adv;

    owed = *acc + lines_per_sec * dt_ms / 1000.0;
    adv = (int) owed;
    if (adv < 1)
    {
        adv = 1;
        *acc = 0.0;
    }
    else
    {
        *acc = owed - adv;
    }
    return adv;
}

/*****************************************************************************/
static cairo_surface_t *
offline_surface(int width, int height, unsigned char **buf_out)
{
    unsigned char *buf;
    int stride;

    stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
    if (stride <= 0)
    {
        return NULL;
    }
    buf = (unsigned char *) calloc((size_t) stride * height, 1);
    if (buf == NULL)
    {
        return NULL;
    }
    *buf_out = buf;
    return cairo_image_surface_create_for_data(buf, CAIRO_FORMAT_RGB24,
            width, height, stride);
}

/*****************************************************************************/
/* --verify: the same N frames down both paths, compared byte for byte.
 *
 * Path A redraws every band from the corpus. Path B scrolls and renders
 * only the exposed strip. A's pixels are the expectation and they are
 * produced by code the strip path does not use, which is what makes
 * this an assertion rather than a restatement.
 *
 * The advance sequence is deliberately irregular and includes 1 (the
 * smallest scroll), a value larger than one screen (which must fall back
 * to a full redraw) and values that do not divide the row count, because
 * an off-by-one in the redrawn band range only shows up at some of them.
 */
static int
run_verify(const struct corpus *cp, int width, int height,
           const char *font, double font_size, long nframes)
{
    static const int adv_seq[] = {1, 2, 7, 13, 25, 40, 3, 100000};
    struct layout lay;
    cairo_surface_t *sa;
    cairo_surface_t *sb;
    cairo_t *ca;
    cairo_t *cb;
    cairo_font_options_t *foa;
    cairo_font_options_t *fob;
    cairo_font_extents_t fext;
    unsigned char *ba;
    unsigned char *bb;
    int stride;
    long f;
    int offset;
    int rc;

    ba = NULL;
    bb = NULL;
    sa = offline_surface(width, height, &ba);
    sb = offline_surface(width, height, &bb);
    if (sa == NULL || sb == NULL)
    {
        fprintf(stderr, "textflood: out of memory for %dx%d\n", width,
                height);
        return 1;
    }
    ca = cairo_create(sa);
    cb = cairo_create(sb);
    foa = setup_font(ca, font, font_size, &fext);
    fob = setup_font(cb, font, font_size, &fext);
    if (layout_init(&lay, width, height, &fext, font_size) != 0)
    {
        return 1;
    }
    stride = cairo_image_surface_get_stride(sa);
    printf("verify: %dx%d, line height %d px (natural %.3f), %d rows, "
           "%ld frames\n", width, height, lay.line_px, fext.height,
           lay.rows, nframes);
    offset = 0;
    render_full_banded(ca, cp, offset, &lay);
    render_full_banded(cb, cp, offset, &lay);
    cairo_surface_flush(sa);
    cairo_surface_flush(sb);
    rc = 0;
    for (f = 0; f < nframes; f++)
    {
        int adv;
        int scrolled;
        int y;

        adv = adv_seq[f % (long) (sizeof(adv_seq) / sizeof(adv_seq[0]))];
        offset = (offset + adv) % cp->nlines;
        render_full_banded(ca, cp, offset, &lay);
        scrolled = render_scroll(cb, sb, cp, offset, adv, &lay) == 0;
        if (!scrolled)
        {
            /* Falling back to the full redraw here would make the two
               surfaces agree by running the SAME code twice, and the
               check would go green having tested nothing. Only an
               advance that legitimately clears the screen may fall
               back; anything else is a failure of the strip path. */
            if (adv <= lay.rows - 2)
            {
                printf("MISMATCH frame %ld: the strip path refused to "
                       "scroll a legal advance of %d lines (%d rows of "
                       "%d px)\n", f, adv, lay.rows, lay.line_px);
                rc = 1;
                break;
            }
            render_full_banded(cb, cp, offset, &lay);
        }
        cairo_surface_flush(sa);
        cairo_surface_flush(sb);
        for (y = 0; y < height; y++)
        {
            const unsigned char *ra;
            const unsigned char *rb;

            ra = ba + (size_t) y * stride;
            rb = bb + (size_t) y * stride;
            if (memcmp(ra, rb, (size_t) width * 4) != 0)
            {
                int x;

                for (x = 0; x < width; x++)
                {
                    if (memcmp(ra + x * 4, rb + x * 4, 4) != 0)
                    {
                        break;
                    }
                }
                printf("MISMATCH frame %ld (advance %d lines, %s, "
                       "offset %d) at pixel x=%d y=%d: full-redraw "
                       "%02x%02x%02x%02x strip %02x%02x%02x%02x\n",
                       f, adv, scrolled ? "scrolled" : "fell back to full",
                       offset, x, y, ra[x * 4 + 3], ra[x * 4 + 2],
                       ra[x * 4 + 1], ra[x * 4 + 0], rb[x * 4 + 3],
                       rb[x * 4 + 2], rb[x * 4 + 1], rb[x * 4 + 0]);
                rc = 1;
                break;
            }
        }
        if (rc != 0)
        {
            break;
        }
    }
    if (rc == 0)
    {
        printf("verify: OK — %ld frames, the scroll+strip path and the "
               "full redraw produced identical bytes\n", nframes);
    }
    cairo_font_options_destroy(foa);
    cairo_font_options_destroy(fob);
    cairo_destroy(ca);
    cairo_destroy(cb);
    cairo_surface_destroy(sa);
    cairo_surface_destroy(sb);
    free(ba);
    free(bb);
    return rc;
}

/*****************************************************************************/
static int
cmp_double(const void *a, const void *b)
{
    double da;
    double db;

    da = *(const double *) a;
    db = *(const double *) b;
    if (da < db)
    {
        return -1;
    }
    return da > db ? 1 : 0;
}

/*****************************************************************************/
static double
pctl(const double *sorted, long n, double q)
{
    long idx;

    if (n <= 0)
    {
        return 0.0;
    }
    idx = (long) (q * (n - 1) + 0.5);
    if (idx < 0)
    {
        idx = 0;
    }
    if (idx >= n)
    {
        idx = n - 1;
    }
    return sorted[idx];
}

/*****************************************************************************/
/* --selftest: FR-BENCH-1's producer telemetry — "its standalone rate
   (--selftest, no RDP session)". Renders into memory with no X server
   and no session, so what it measures is the producer's own compute and
   nothing else. The X-side XShmPutImage and XSync are NOT in it; the
   deployed rate is this plus one frame-sized copy on the X thread. */
static int
run_selftest(const struct corpus *cp, int width, int height,
             const char *font, double font_size, int mode, int step,
             double lines_per_sec, long frames, double pipeline_ms)
{
    struct layout lay;
    cairo_surface_t *surf;
    cairo_t *cr;
    cairo_font_options_t *fo;
    cairo_font_extents_t fext;
    unsigned char *buf;
    double *samples;
    double line_height;
    double acc;
    double t_prev;
    double total;
    double mean;
    double lines_total;
    long f;
    long warm;
    long nsam;
    int offset;
    int rows_full;

    warm = 2;
    if (frames < warm + 8)
    {
        frames = warm + 8;
    }
    buf = NULL;
    surf = offline_surface(width, height, &buf);
    if (surf == NULL)
    {
        fprintf(stderr, "textflood: out of memory for %dx%d\n", width,
                height);
        return 1;
    }
    samples = (double *) malloc(sizeof(double) * frames);
    if (samples == NULL)
    {
        return 1;
    }
    cr = cairo_create(surf);
    fo = setup_font(cr, font, font_size, &fext);
    line_height = fext.height;
    if (line_height < 1.0)
    {
        line_height = font_size;
    }
    rows_full = (int) (height / line_height) + 1;
    if (mode == MODE_STRIP && layout_init(&lay, width, height, &fext,
                                          font_size) != 0)
    {
        return 1;
    }
    printf("selftest: %dx%d, %s, corpus %d lines, font %s %.0fpx, "
           "subpixel RGB\n", width, height,
           mode == MODE_STRIP ? "--scroll strip (memmove + strip render)"
           : "--scroll full (every row re-rendered)", cp->nlines, font,
           font_size);
    if (mode == MODE_STRIP)
    {
        printf("selftest: line height pinned to %d px (natural %.3f), "
               "%d rows, content %.1f lines/s\n", lay.line_px, fext.height,
               lay.rows, lines_per_sec);
    }
    else
    {
        printf("selftest: line height %.3f px, %d rows, %d lines per "
               "frame\n", line_height, rows_full, step);
    }
    printf("selftest: %ld frames (%ld discarded to warm the glyph "
           "cache)\n", frames, warm);
    fflush(stdout);
    offset = 0;
    acc = 0.0;
    nsam = 0;
    lines_total = 0.0;
    t_prev = now_ms();
    for (f = 0; f < frames; f++)
    {
        double t0;
        double t1;
        int adv;

        t0 = now_ms();
        adv = step;
        if (mode == MODE_STRIP)
        {
            if (f == 0)
            {
                adv = 0;
                acc = 0.0;
                render_full_banded(cr, cp, offset, &lay);
            }
            else
            {
                int rc;

                adv = advance_lines(&acc, t0 - t_prev, lines_per_sec);
                offset = (offset + adv) % cp->nlines;
                rc = render_scroll(cr, surf, cp, offset, adv, &lay);
                if (rc < 0)
                {
                    return 1;
                }
                if (rc > 0)
                {
                    render_full_banded(cr, cp, offset, &lay);
                }
            }
        }
        else
        {
            draw_frame(cr, cp, offset, width, rows_full, line_height,
                       fext.ascent);
            offset = (offset + step) % cp->nlines;
        }
        cairo_surface_flush(surf);
        t1 = now_ms();
        t_prev = t0;
        if (f >= warm)
        {
            samples[nsam++] = t1 - t0;
            lines_total += adv;
        }
    }
    total = 0.0;
    for (f = 0; f < nsam; f++)
    {
        total += samples[f];
    }
    mean = nsam > 0 ? total / nsam : 0.0;
    qsort(samples, nsam, sizeof(double), cmp_double);
    printf("\n  frames measured      %ld\n", nsam);
    printf("  ms/frame mean        %.3f\n", mean);
    printf("  ms/frame p50         %.3f\n", pctl(samples, nsam, 0.50));
    printf("  ms/frame p90         %.3f\n", pctl(samples, nsam, 0.90));
    printf("  ms/frame p99         %.3f\n", pctl(samples, nsam, 0.99));
    printf("  ms/frame max         %.3f\n", pctl(samples, nsam, 1.00));
    printf("  standalone rate      %.1f fps (from the mean)\n",
           mean > 0.0 ? 1000.0 / mean : 0.0);
    printf("  corpus lines/frame   %.2f mean\n",
           nsam > 0 ? lines_total / nsam : 0.0);
    printf("  content speed        %.1f lines/s over the measured "
           "frames\n", total > 0.0 ? lines_total * 1000.0 / total : 0.0);
    if (pipeline_ms > 0.0 && mean > 0.0)
    {
        double margin;

        margin = pipeline_ms / mean;
        printf("\nFR-BENCH-1 against a %.3f ms pipeline period "
               "(%.1f sends/s):\n", pipeline_ms, 1000.0 / pipeline_ms);
        printf("  producer margin      %.2fx  %s\n", margin,
               margin >= 2.0 ? "(>= 2.0x floor)" : "(BELOW the 2.0x "
               "floor — print this margin beside every ratio)");
    }
    free(samples);
    cairo_font_options_destroy(fo);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    free(buf);
    return 0;
}

/*****************************************************************************/
static void
usage(void)
{
    printf("textflood — client-side-rendered scrolling code payload\n\n"
           "  --corpus PATH   ANSI corpus (default %s)\n"
           "  --title NAME    window name (default %s)\n"
           "  --font NAME     font family (default \"%s\")\n"
           "  --size N        font size in px (default %.0f)\n"
           "  --scroll MODE   full (default; the historical redraw, and\n"
           "                  what every archived capture was measured\n"
           "                  with) or strip (BACKLOG #83 design B:\n"
           "                  memmove the frame up, render only the\n"
           "                  newly exposed bottom strip)\n"
           "  --step N        lines advanced per frame, --scroll full\n"
           "                  only (default %d)\n"
           "  --lines-per-sec R  content speed, --scroll strip only\n"
           "                  (default %.1f = %d lines / 16.901 ms, the\n"
           "                  speed the full-redraw payload ran at)\n"
           "  --frames N      stop after N frames (default: forever)\n"
           "  --stamps PATH   per-frame timing tsv (default %s;\n"
           "                  FR-BENCH-1 producer telemetry)\n"
           "  --managed       let the window manager place the window\n"
           "                  (default: override-redirect, full root)\n"
           "  --monitor N     render only RandR output N (default:\n"
           "                  all monitors); monitor geometry is queried\n"
           "                  from the live X server\n"
           "\n"
           "  offline, no X server and no session:\n"
           "  --selftest      measure this payload's own frame rate and\n"
           "                  print the FR-BENCH-1 margin (required by\n"
           "                  PRD FR-BENCH-1)\n"
           "  --verify N      render N frames down BOTH scroll paths and\n"
           "                  compare byte for byte; non-zero exit on\n"
           "                  the first differing pixel\n"
           "  --geometry WxH  geometry for the offline modes (default\n"
           "                  %dx%d)\n"
           "  --pipeline-ms M pipeline frame period the --selftest\n"
           "                  margin is taken against (default %.3f)\n"
           "  -h, --help      this text\n",
           DEF_CORPUS, DEF_TITLE, DEF_FONT, DEF_FONT_SIZE, DEF_STEP,
           DEF_LINES_PER_SEC, DEF_STEP, DEF_STAMPS, DEF_OFF_W, DEF_OFF_H,
           DEF_PIPELINE_MS);
}

/*****************************************************************************/
int
main(int argc, char **argv)
{
    const char *corpus_path;
    const char *title;
    const char *font;
    double font_size;
    int step;
    long max_frames;
    int managed;
    int ai;
    struct corpus cp;
    Display *dpy;
    int screen;
    Window root;
    Window win;
    Visual *visual;
    int depth;
    XSetWindowAttributes attr;
    XWindowAttributes root_attr;
    XShmSegmentInfo shminfo;
    XImage *image;
    GC gc;
    cairo_surface_t *surf;
    cairo_t *cr;
    cairo_font_options_t *fo;
    cairo_font_extents_t fext;
    int width;
    int height;
    int rows;
    int offset;
    long frame;
    double line_height;
    const char *stamps_path;
    FILE *stf;
    double t_loop;
    double t_render;
    double t_blit;
    double t_sync;
    int mode;
    double lines_per_sec;
    double pipeline_ms;
    int off_w;
    int off_h;
    int selftest;
    long verify_frames;
    struct layout lay;
    double acc;
    double t_prev;
    int need_full;
    int monitor;
    int origin_x;
    int origin_y;

    stamps_path = DEF_STAMPS;
    corpus_path = DEF_CORPUS;
    title = DEF_TITLE;
    font = DEF_FONT;
    font_size = DEF_FONT_SIZE;
    step = DEF_STEP;
    max_frames = 0;
    managed = 0;
    mode = MODE_FULL;
    lines_per_sec = DEF_LINES_PER_SEC;
    pipeline_ms = DEF_PIPELINE_MS;
    off_w = DEF_OFF_W;
    off_h = DEF_OFF_H;
    selftest = 0;
    verify_frames = 0;
    monitor = -1;
    for (ai = 1; ai < argc; ai++)
    {
        if (strcmp(argv[ai], "-h") == 0 || strcmp(argv[ai], "--help") == 0)
        {
            usage();
            return 0;
        }
        else if (strcmp(argv[ai], "--managed") == 0)
        {
            managed = 1;
        }
        else if (strcmp(argv[ai], "--selftest") == 0)
        {
            selftest = 1;
        }
        else if (ai + 1 >= argc)
        {
            fprintf(stderr, "textflood: %s needs a value\n", argv[ai]);
            return 1;
        }
        else if (strcmp(argv[ai], "--corpus") == 0)
        {
            corpus_path = argv[++ai];
        }
        else if (strcmp(argv[ai], "--title") == 0)
        {
            title = argv[++ai];
        }
        else if (strcmp(argv[ai], "--font") == 0)
        {
            font = argv[++ai];
        }
        else if (strcmp(argv[ai], "--size") == 0)
        {
            font_size = atof(argv[++ai]);
        }
        else if (strcmp(argv[ai], "--step") == 0)
        {
            step = atoi(argv[++ai]);
        }
        else if (strcmp(argv[ai], "--frames") == 0)
        {
            max_frames = atol(argv[++ai]);
        }
        else if (strcmp(argv[ai], "--stamps") == 0)
        {
            stamps_path = argv[++ai];
        }
        else if (strcmp(argv[ai], "--scroll") == 0)
        {
            ai++;
            if (strcmp(argv[ai], "full") == 0)
            {
                mode = MODE_FULL;
            }
            else if (strcmp(argv[ai], "strip") == 0)
            {
                mode = MODE_STRIP;
            }
            else
            {
                fprintf(stderr, "textflood: --scroll takes full or "
                        "strip, not %s\n", argv[ai]);
                return 1;
            }
        }
        else if (strcmp(argv[ai], "--lines-per-sec") == 0)
        {
            lines_per_sec = atof(argv[++ai]);
            if (!(lines_per_sec > 0.0))
            {
                fprintf(stderr, "textflood: --lines-per-sec must be "
                        "positive\n");
                return 1;
            }
        }
        else if (strcmp(argv[ai], "--pipeline-ms") == 0)
        {
            pipeline_ms = atof(argv[++ai]);
        }
        else if (strcmp(argv[ai], "--monitor") == 0)
        {
            char *end;
            long value;

            value = strtol(argv[++ai], &end, 10);
            if (*argv[ai] == 0 || *end != 0 || value < 0 || value > 15)
            {
                fprintf(stderr, "textflood: --monitor wants an integer "
                        "within 0..15, not %s\n", argv[ai]);
                return 1;
            }
            monitor = (int) value;
        }
        else if (strcmp(argv[ai], "--verify") == 0)
        {
            verify_frames = atol(argv[++ai]);
            if (verify_frames <= 0)
            {
                fprintf(stderr, "textflood: --verify needs a positive "
                        "frame count\n");
                return 1;
            }
        }
        else if (strcmp(argv[ai], "--geometry") == 0)
        {
            ai++;
            if (sscanf(argv[ai], "%dx%d", &off_w, &off_h) != 2
                    || off_w < 64 || off_h < 64 || off_w > 16384
                    || off_h > 16384)
            {
                fprintf(stderr, "textflood: --geometry wants WxH within "
                        "64..16384, not %s\n", argv[ai]);
                return 1;
            }
        }
        else
        {
            fprintf(stderr, "textflood: unknown option %s\n", argv[ai]);
            return 1;
        }
    }
    if (step < 1)
    {
        fprintf(stderr, "textflood: --step must be at least 1\n");
        return 1;
    }
    if (corpus_load(&cp, corpus_path) != 0)
    {
        return 1;
    }
    /* the offline modes never open a display: they exist so the strip
       path can be proved correct and timed on any box, in seconds, with
       no session to perturb */
    if (verify_frames > 0)
    {
        return run_verify(&cp, off_w, off_h, font, font_size,
                          verify_frames);
    }
    if (selftest)
    {
        if (monitor >= 0)
        {
            fprintf(stderr, "textflood: --monitor is a live-display "
                    "option\n");
            return 1;
        }
        return run_selftest(&cp, off_w, off_h, font, font_size, mode,
                            step, lines_per_sec,
                            max_frames > 0 ? max_frames : 200,
                            pipeline_ms);
    }
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "textflood: cannot open DISPLAY\n");
        return 1;
    }
    if (!XShmQueryExtension(dpy))
    {
        /* Without MIT-SHM every frame would cross the X socket, which
           would put the copy back on the wire and defeat the purpose.
           Fail rather than silently measure something else. */
        fprintf(stderr, "textflood: X server has no MIT-SHM\n");
        return 1;
    }
    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);
    visual = DefaultVisual(dpy, screen);
    depth = DefaultDepth(dpy, screen);
    if (depth != 24 && depth != 32)
    {
        fprintf(stderr, "textflood: need a 24/32-bit screen, got %d\n", depth);
        return 1;
    }
    if (visual->red_mask != 0x00ff0000 || visual->green_mask != 0x0000ff00
            || visual->blue_mask != 0x000000ff)
    {
        /* cairo's RGB24 is native-endian 0xRRGGBB; anything else would
           come out with the channels swapped, which on a colour-accuracy
           benchmark is worse than not running. */
        fprintf(stderr, "textflood: unexpected visual masks\n");
        return 1;
    }
    /* The default is the whole root. --monitor deliberately produces the
       one-active/one-idle condition while keeping the same renderer. Query
       the live RandR output geometry rather than baking a fleet modeline into
       the payload. An override-redirect window is not managed, so there is
       nothing to re-snap and no span-fixer loop to keep running. */
    XGetWindowAttributes(dpy, root, &root_attr);
    width = root_attr.width;
    height = root_attr.height;
    origin_x = 0;
    origin_y = 0;
    if (monitor >= 0)
    {
        struct monitor_geometry geometry = {0};
        int count;
        int retry;

        /* xorgxrdp initially advertises one union output and replaces it
           with the client monitor outputs when the RDP layout arrives. Do
           not mistake that transient union for monitor zero. This wait is
           before the stamps file and measured render loop are started. */
        count = 0;
        for (retry = 0; retry < 100; retry++)
        {
            count = query_monitor_geometry(dpy, root, monitor, &geometry);
            if (count >= 2 && monitor < count)
            {
                break;
            }
            usleep(100000);
        }
        if (count < 2 || monitor >= count)
        {
            fprintf(stderr, "textflood: monitor %d unavailable "
                    "after waiting for the multimon layout (RandR reports "
                    "%d active outputs)\n", monitor, count);
            return 1;
        }
        origin_x = geometry.x;
        origin_y = geometry.y;
        width = geometry.width;
        height = geometry.height;
    }
    memset(&attr, 0, sizeof(attr));
    attr.override_redirect = managed ? False : True;
    attr.background_pixel = BlackPixel(dpy, screen);
    attr.event_mask = KeyPressMask | ExposureMask;
    win = XCreateWindow(dpy, root, origin_x, origin_y, width, height, 0, depth,
                        InputOutput, visual,
                        CWOverrideRedirect | CWBackPixel | CWEventMask,
                        &attr);
    XStoreName(dpy, win, title);
    /* the map is DEFERRED to the top of the render loop: mapping here
       presents a black full-root window for the whole SHM/corpus/font
       setup, and the session's capture timer reliably catches it as one
       black frame (oracle black-frame check FAILs, 2026-07-31, pictures
       62/68). The window is mapped only when frame 0 is ready to blit. */
    gc = XCreateGC(dpy, win, 0, NULL);
    memset(&shminfo, 0, sizeof(shminfo));
    image = XShmCreateImage(dpy, visual, depth, ZPixmap, NULL, &shminfo,
                            width, height);
    if (image == NULL)
    {
        fprintf(stderr, "textflood: XShmCreateImage failed\n");
        return 1;
    }
    shminfo.shmid = shmget(IPC_PRIVATE,
                           (size_t) image->bytes_per_line * image->height,
                           IPC_CREAT | 0600);
    if (shminfo.shmid < 0)
    {
        fprintf(stderr, "textflood: shmget failed\n");
        return 1;
    }
    shminfo.shmaddr = (char *) shmat(shminfo.shmid, NULL, 0);
    image->data = shminfo.shmaddr;
    shminfo.readOnly = False;
    if (shminfo.shmaddr == (char *) -1 || !XShmAttach(dpy, &shminfo))
    {
        fprintf(stderr, "textflood: XShmAttach failed\n");
        return 1;
    }
    XSync(dpy, False);
    /* mark it destroyed now so the segment goes away even if we are
       killed at logoff */
    shmctl(shminfo.shmid, IPC_RMID, NULL);
    surf = cairo_image_surface_create_for_data((unsigned char *) image->data,
           CAIRO_FORMAT_RGB24, width, height, image->bytes_per_line);
    cr = cairo_create(surf);
    fo = setup_font(cr, font, font_size, &fext);
    line_height = fext.height;
    if (line_height < 1.0)
    {
        line_height = font_size;
    }
    rows = (int) (height / line_height) + 1;
    if (monitor < 0)
    {
        printf("textflood: target=all origin=%d,%d %dx%d, %d rows, "
               "%d corpus lines, step %d, font %s %.0fpx, subpixel RGB\n",
               origin_x, origin_y, width, height, rows, cp.nlines, step,
               font, font_size);
    }
    else
    {
        printf("textflood: target=monitor-%d origin=%d,%d %dx%d, %d rows, "
               "%d corpus lines, step %d, font %s %.0fpx, subpixel RGB\n",
               monitor, origin_x, origin_y, width, height, rows, cp.nlines,
               step, font, font_size);
    }
    if (mode == MODE_STRIP)
    {
        if (layout_init(&lay, width, height, &fext, font_size) != 0)
        {
            return 1;
        }
        rows = lay.rows;
        printf("textflood: --scroll strip: line height pinned to %d px "
               "(natural %.3f), %d rows, content %.1f lines/s\n",
               lay.line_px, fext.height, lay.rows, lines_per_sec);
    }
    fflush(stdout);
    stf = NULL;
    if (stamps_path[0] != 0)
    {
        stf = fopen(stamps_path, "w");
        if (stf == NULL)
        {
            fprintf(stderr, "textflood: cannot write stamps to %s "
                    "(continuing without)\n", stamps_path);
        }
        else
        {
            struct timespec rt;

            clock_gettime(CLOCK_REALTIME, &rt);
            /* epoch anchor so a frame's monotonic stamp can be laid
               next to the server's GFX_TRACE wall clock */
            if (monitor < 0)
            {
                fprintf(stf, "# textflood target=all origin=%d,%d %dx%d "
                        "step=%d epoch_ms=%.3f mono_ms=%.3f\n", origin_x,
                        origin_y, width, height, step,
                        rt.tv_sec * 1000.0 + rt.tv_nsec / 1e6, now_ms());
            }
            else
            {
                fprintf(stf, "# textflood target=monitor-%d origin=%d,%d "
                        "%dx%d step=%d epoch_ms=%.3f mono_ms=%.3f\n",
                        monitor, origin_x, origin_y, width, height, step,
                        rt.tv_sec * 1000.0 + rt.tv_nsec / 1e6, now_ms());
            }
            fprintf(stf, "frame\tloop_start_ms\trender_ms\tblit_ms"
                    "\tsync_ms\n");
        }
    }
    offset = 0;
    frame = 0;
    acc = 0.0;
    t_prev = now_ms();
    need_full = 1;
    for (;;)
    {
        while (XPending(dpy) > 0)
        {
            XEvent ev;

            XNextEvent(dpy, &ev);
            if (ev.type == KeyPress)
            {
                KeySym ks;

                ks = XLookupKeysym(&ev.xkey, 0);
                if (ks == XK_Escape || ks == XK_q)
                {
                    goto done;
                }
            }
            else if (ev.type == Expose)
            {
                /* the window never resizes (override-redirect at the
                   root size), but if anything ever invalidates what we
                   believe is on screen, the next frame is a full
                   redraw rather than a scroll of stale pixels */
                need_full = 1;
            }
        }
        t_loop = now_ms();
        if (mode == MODE_STRIP)
        {
            int adv;

            adv = advance_lines(&acc, t_loop - t_prev, lines_per_sec);
            t_prev = t_loop;
            if (need_full)
            {
                acc = 0.0;
                render_full_banded(cr, &cp, offset, &lay);
                need_full = 0;
            }
            else
            {
                int rc;

                offset = (offset + adv) % cp.nlines;
                rc = render_scroll(cr, surf, &cp, offset, adv, &lay);
                if (rc < 0)
                {
                    /* a broken invariant is a bug in this file, and a
                       payload that quietly redrew instead would keep
                       running and keep being measured */
                    return 1;
                }
                if (rc > 0)
                {
                    render_full_banded(cr, &cp, offset, &lay);
                }
            }
        }
        else
        {
            draw_frame(cr, &cp, offset, width, rows, line_height,
                       fext.ascent);
        }
        cairo_surface_flush(surf);
        t_render = now_ms();
        if (frame == 0)
        {
            /* first frame is rendered: map and blit in one request batch
               so no bare-background window is ever presented (see the
               deferred-map comment at XCreateWindow) */
            XMapRaised(dpy, win);
        }
        XShmPutImage(dpy, win, gc, image, 0, 0, 0, 0, width, height, False);
        t_blit = now_ms();
        /* XSync, not XFlush: without it the client races ahead of the
           server and the measured send interval becomes this program's
           loop rate rather than the pipeline's. */
        XSync(dpy, False);
        t_sync = now_ms();
        if (stf != NULL)
        {
            /* the split #65 step 0 exists to measure: how much of the
               producer's period is its own render, and how much is
               waiting on the X server inside XSync */
            fprintf(stf, "%ld\t%.3f\t%.3f\t%.3f\t%.3f\n", frame, t_loop,
                    t_render - t_loop, t_blit - t_render,
                    t_sync - t_blit);
            if ((frame & 15) == 0)
            {
                fflush(stf);
            }
        }
        if (mode == MODE_FULL)
        {
            offset = (offset + step) % cp.nlines;
        }
        frame++;
        if (max_frames > 0 && frame >= max_frames)
        {
            break;
        }
    }
done:
    if (stf != NULL)
    {
        fclose(stf);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    cairo_font_options_destroy(fo);
    XShmDetach(dpy, &shminfo);
    XDestroyImage(image);
    shmdt(shminfo.shmaddr);
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
