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
 * Build: PR-demo/textflood/build.sh   (cairo + X11 + Xext, no toolkit)
 * Usage: textflood [options]; -h for the list.
 */

#include <sys/ipc.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
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
#define DEF_TITLE "E52FLOOD"
#define DEF_FONT "DejaVu Sans Mono"
#define DEF_FONT_SIZE 14.0
#define DEF_STEP 25

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
/* Draw one frame. Every visible row is filled edge to edge by repeating
   the corpus line horizontally — the xterm payload got the same effect by
   repeating each line 32 times and letting the terminal wrap it, and the
   point is the same: no row is mostly background, so a frame is a
   full-width content change rather than a sparse one. */
static void
draw_frame(cairo_t *cr, const struct corpus *cp, int offset,
           int width, int rows, double line_height, double baseline)
{
    int row;
    int li;
    int ri;
    double x;
    double y;
    cairo_text_extents_t ext;

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
        y = row * line_height + baseline;
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
           "  --step N        lines advanced per frame (default %d)\n"
           "  --frames N      stop after N frames (default: forever)\n"
           "  --stamps PATH   per-frame timing tsv (default %s;\n"
           "                  FR-BENCH-1 producer telemetry)\n"
           "  --managed       let the window manager place the window\n"
           "                  (default: override-redirect, full root)\n"
           "  -h, --help      this text\n",
           DEF_CORPUS, DEF_TITLE, DEF_FONT, DEF_FONT_SIZE, DEF_STEP,
           DEF_STAMPS);
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

    stamps_path = DEF_STAMPS;
    corpus_path = DEF_CORPUS;
    title = DEF_TITLE;
    font = DEF_FONT;
    font_size = DEF_FONT_SIZE;
    step = DEF_STEP;
    max_frames = 0;
    managed = 0;
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
        else
        {
            fprintf(stderr, "textflood: unknown option %s\n", argv[ai]);
            return 1;
        }
    }
    if (corpus_load(&cp, corpus_path) != 0)
    {
        return 1;
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
    /* THE WHOLE ROOT, NOT ONE MONITOR. The E5-2 gate is about batching
       two monitors into one pump set, so a payload that inks only one is
       measuring BACKLOG #53's one-active-one-idle regime instead. Three
       T4 runs were invalidated that way with the xterm payload, because
       to xfwm4 "maximized" means the current monitor and it re-snaps a
       window on its own schedule. An override-redirect window is not
       managed at all, so there is nothing to re-snap and no span-fixer
       loop to keep running. */
    XGetWindowAttributes(dpy, root, &root_attr);
    width = root_attr.width;
    height = root_attr.height;
    memset(&attr, 0, sizeof(attr));
    attr.override_redirect = managed ? False : True;
    attr.background_pixel = BlackPixel(dpy, screen);
    attr.event_mask = KeyPressMask | ExposureMask;
    win = XCreateWindow(dpy, root, 0, 0, width, height, 0, depth,
                        InputOutput, visual,
                        CWOverrideRedirect | CWBackPixel | CWEventMask,
                        &attr);
    XStoreName(dpy, win, title);
    XMapRaised(dpy, win);
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
    cairo_font_extents(cr, &fext);
    line_height = fext.height;
    if (line_height < 1.0)
    {
        line_height = font_size;
    }
    rows = (int) (height / line_height) + 1;
    printf("textflood: %dx%d, %d rows, %d corpus lines, step %d, "
           "font %s %.0fpx, subpixel RGB\n",
           width, height, rows, cp.nlines, step, font, font_size);
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
            fprintf(stf, "# textflood %dx%d step=%d epoch_ms=%.3f "
                    "mono_ms=%.3f\n", width, height, step,
                    rt.tv_sec * 1000.0 + rt.tv_nsec / 1e6, now_ms());
            fprintf(stf, "frame\tloop_start_ms\trender_ms\tblit_ms"
                    "\tsync_ms\n");
        }
    }
    offset = 0;
    frame = 0;
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
        }
        t_loop = now_ms();
        draw_frame(cr, &cp, offset, width, rows, line_height, fext.ascent);
        cairo_surface_flush(surf);
        t_render = now_ms();
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
        offset = (offset + step) % cp.nlines;
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
