/* Full-screen X11 colour/motion test for ONSCREEN judgement by a human
 * on a real client (Windows UWP, macOS). It is the X11 sibling of
 * colorkey.sh and exists because that script draws through a TERMINAL:
 * it can only address character cells, so its sliding block hops ~24 px
 * at a time and its "colour" is whatever the terminal palette renders.
 * This app owns the pixels.
 *
 * KEYS (act immediately, in every mode):
 *   r g b w  paint the whole screen red / green / blue / white
 *   e        colour-EDGE pattern: 1 px alternating red/blue columns.
 *            Flat colours look identical at 4:2:0 and 4:4:4; 1 px
 *            opposing-chroma columns wash to a flat violet at 4:2:0 and
 *            stay separated at 4:4:4.
 *   c        EIGHT-COLOUR CYCLE, one colour per frame, in the fixed
 *            order black red green blue yellow magenta cyan white, at
 *            CK_CYCLE_MS ms per frame (default 250). The colour name,
 *            the step 1..8, the cycle number and the frame count are
 *            drawn on every frame.
 *   s        SLIDING BLOCK: a white block on blue, stepping CK_SLIDE_PX
 *            pixels right per frame (default 8) at CK_SLIDE_MS ms per
 *            frame (default 40 = 25 frames/s), wrapping at the right
 *            edge. Unlike the terminal version this is a PIXEL step.
 *   q  Esc   quit
 *
 * HOW TO READ IT -- the point of the app, so it is stated on screen and
 * here rather than left to the eye:
 *   c: a colour out of order, or the step index jumping by more than 1,
 *      means frames were reordered or dropped. A colour that never
 *      appears means a frame was withheld. A WRONG HUE with the order
 *      intact (yellow looking green, magenta looking blue) is a chroma
 *      plane problem in the encoder or decoder, NOT flow control -- the
 *      eight corners are chosen so a dropped or swapped chroma plane
 *      changes the colour NAME, not a subtle tint.
 *   s: even hops of identical spacing every frame is healthy; a pause
 *      then a resume at the next position is a late or withheld frame;
 *      the block reappearing several hops further right, having never
 *      been drawn in between, is dropped frames.
 *   e: any wash to flat violet is chroma running at 4:2:0.
 *
 * Every animated frame is ONE full-screen fill -- one damage, one GFX
 * frame -- and is appended to $CK_LOG (default /tmp/colorkey_x11.log)
 * so the log says what the screen SHOULD show at any instant.
 *
 * build: gcc -O2 colorkey_x11.c -lX11 -o colorkey_x11
 * run:   DISPLAY=:10 ./colorkey_x11
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MODE_FLAT   0
#define MODE_EDGE   1
#define MODE_CYCLE  2
#define MODE_SLIDE  3

struct named_rgb
{
    const char *name;
    unsigned long r;
    unsigned long g;
    unsigned long b;
};

/* the eight RGB corners, in the order the cycle walks them */
static const struct named_rgb g_cycle[8] =
{
    { "black",   0,   0,   0 },
    { "red",   255,   0,   0 },
    { "green",   0, 255,   0 },
    { "blue",    0,   0, 255 },
    { "yellow", 255, 255,   0 },
    { "magenta", 255,   0, 255 },
    { "cyan",    0, 255, 255 },
    { "white", 255, 255, 255 }
};

static long long
now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int
env_int(const char *name, int fallback)
{
    const char *v = getenv(name);
    int n;

    if (v == NULL || *v == 0)
    {
        return fallback;
    }
    n = atoi(v);
    return n > 0 ? n : fallback;
}

/* pack an 8-bit-per-channel colour for this visual */
static unsigned long
pack(Visual *vis, unsigned long r, unsigned long g, unsigned long b)
{
    unsigned long rv = 0;
    int shift;
    unsigned long m;

    m = vis->red_mask;
    for (shift = 0; m != 0 && (m & 1) == 0; m >>= 1)
    {
        shift++;
    }
    rv |= (r << shift) & vis->red_mask;
    m = vis->green_mask;
    for (shift = 0; m != 0 && (m & 1) == 0; m >>= 1)
    {
        shift++;
    }
    rv |= (g << shift) & vis->green_mask;
    m = vis->blue_mask;
    for (shift = 0; m != 0 && (m & 1) == 0; m >>= 1)
    {
        shift++;
    }
    rv |= (b << shift) & vis->blue_mask;
    return rv;
}

/* Load the largest readable font available. A 4K screen viewed on a
 * laptop needs a big one, and the server may have only "fixed". */
static XFontStruct *
load_font(Display *dpy)
{
    static const char *tries[] =
    {
        "-*-helvetica-bold-r-normal--34-*-*-*-*-*-iso8859-1",
        "-*-*-bold-r-normal--34-*-*-*-*-*-iso8859-1",
        "-*-*-bold-r-normal--24-*-*-*-*-*-iso8859-1",
        "10x20",
        "9x15",
        "fixed",
        NULL
    };
    XFontStruct *f;
    int i;

    for (i = 0; tries[i] != NULL; i++)
    {
        f = XLoadQueryFont(dpy, tries[i]);
        if (f != NULL)
        {
            return f;
        }
    }
    return NULL;
}

static void
log_frame(FILE *lf, long long n, const char *what)
{
    if (lf == NULL)
    {
        return;
    }
    fprintf(lf, "%lld %lld %s\n", now_ms(), n, what);
    fflush(lf);
}

int
main(int argc, char **argv)
{
    Display *dpy;
    Window win;
    GC gc;
    Visual *vis;
    XFontStruct *font;
    XEvent ev;
    FILE *lf;
    const char *logpath;
    char line[256];
    int screen;
    int sw;
    int sh;
    int mode = MODE_FLAT;
    int cycle_ms;
    int slide_ms;
    int slide_px;
    int block_w;
    int block_h;
    int step = 0;
    int cycles = 0;
    int slide_x = 0;
    long long n = 0;
    long long next_due = 0;
    unsigned long flat;
    int text_y;

    (void)argc;
    (void)argv;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "colorkey_x11: cannot open DISPLAY=%s\n",
                getenv("DISPLAY") ? getenv("DISPLAY") : "(unset)");
        return 1;
    }
    screen = DefaultScreen(dpy);
    vis = DefaultVisual(dpy, screen);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);

    cycle_ms = env_int("CK_CYCLE_MS", 250);
    slide_ms = env_int("CK_SLIDE_MS", 40);
    slide_px = env_int("CK_SLIDE_PX", 8);
    block_w = env_int("CK_BLOCK_W", sw / 10);
    block_h = env_int("CK_BLOCK_H", sh / 10);
    if (block_w < 16)
    {
        block_w = 16;
    }
    if (block_h < 16)
    {
        block_h = 16;
    }

    logpath = getenv("CK_LOG");
    if (logpath == NULL || *logpath == 0)
    {
        logpath = "/tmp/colorkey_x11.log";
    }
    lf = fopen(logpath, "a");

    /* override-redirect so no window manager decorates or resizes it:
     * the whole screen must be ours, or "one fill = one damage" is a
     * lie and the counts below mean nothing */
    {
        XSetWindowAttributes at;

        at.override_redirect = True;
        at.background_pixel = BlackPixel(dpy, screen);
        at.event_mask = ExposureMask | KeyPressMask;
        win = XCreateWindow(dpy, RootWindow(dpy, screen), 0, 0, sw, sh, 0,
                            CopyFromParent, InputOutput, CopyFromParent,
                            CWOverrideRedirect | CWBackPixel | CWEventMask,
                            &at);
    }
    XStoreName(dpy, win, "colorkey_x11");
    XMapRaised(dpy, win);
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);
    gc = XCreateGC(dpy, win, 0, NULL);
    font = load_font(dpy);
    if (font != NULL)
    {
        XSetFont(dpy, gc, font->fid);
    }
    text_y = font != NULL ? font->ascent + 16 : 30;

    flat = pack(vis, 0, 0, 0);
    printf("colorkey_x11: %dx%d on %s; keys r g b w e c s q; log %s\n",
           sw, sh, XDisplayString(dpy), logpath);
    fflush(stdout);

    for (;;)
    {
        int have_key = 0;
        KeySym ks = 0;

        /* drain input first so a key always acts on the NEXT frame */
        while (XPending(dpy) > 0)
        {
            XNextEvent(dpy, &ev);
            if (ev.type == KeyPress)
            {
                ks = XLookupKeysym(&ev.xkey, 0);
                have_key = 1;
            }
            else if (ev.type == Expose && mode == MODE_FLAT)
            {
                XSetForeground(dpy, gc, flat);
                XFillRectangle(dpy, win, gc, 0, 0, sw, sh);
            }
        }
        if (have_key)
        {
            switch (ks)
            {
                case XK_q:
                case XK_Escape:
                    goto done;
                case XK_r:
                    mode = MODE_FLAT;
                    flat = pack(vis, 255, 0, 0);
                    break;
                case XK_g:
                    mode = MODE_FLAT;
                    flat = pack(vis, 0, 255, 0);
                    break;
                case XK_b:
                    mode = MODE_FLAT;
                    flat = pack(vis, 0, 0, 255);
                    break;
                case XK_w:
                    mode = MODE_FLAT;
                    flat = pack(vis, 255, 255, 255);
                    break;
                case XK_e:
                    mode = MODE_EDGE;
                    break;
                case XK_c:
                    mode = MODE_CYCLE;
                    next_due = 0;
                    break;
                case XK_s:
                    mode = MODE_SLIDE;
                    next_due = 0;
                    break;
                default:
                    break;
            }
            if (mode == MODE_FLAT || mode == MODE_EDGE)
            {
                next_due = 0;      /* draw once, immediately */
            }
        }

        if (mode == MODE_FLAT)
        {
            if (next_due == 0)
            {
                n++;
                XSetForeground(dpy, gc, flat);
                XFillRectangle(dpy, win, gc, 0, 0, sw, sh);
                XSetForeground(dpy, gc, pack(vis, 128, 128, 128));
                snprintf(line, sizeof(line), "flat  n=%lld", n);
                XDrawString(dpy, win, gc, 20, text_y, line,
                            (int)strlen(line));
                XFlush(dpy);
                log_frame(lf, n, "flat");
                next_due = -1;     /* nothing further until a key */
            }
            usleep(5000);
            continue;
        }
        if (mode == MODE_EDGE)
        {
            if (next_due == 0)
            {
                int x;

                n++;
                for (x = 0; x < sw; x++)
                {
                    XSetForeground(dpy, gc,
                                   (x & 1) ? pack(vis, 0, 0, 255)
                                   : pack(vis, 255, 0, 0));
                    XFillRectangle(dpy, win, gc, x, 0, 1, sh);
                }
                XSetForeground(dpy, gc, pack(vis, 255, 255, 255));
                snprintf(line, sizeof(line),
                         "edge 1px red/blue  n=%lld  "
                         "(flat violet = chroma at 4:2:0)", n);
                XDrawString(dpy, win, gc, 20, text_y, line,
                            (int)strlen(line));
                XFlush(dpy);
                log_frame(lf, n, "edge");
                next_due = -1;
            }
            usleep(5000);
            continue;
        }
        if (mode == MODE_CYCLE)
        {
            long long t = now_ms();

            if (next_due <= 0 || t >= next_due)
            {
                const struct named_rgb *c = &g_cycle[step];

                n++;
                XSetForeground(dpy, gc, pack(vis, c->r, c->g, c->b));
                XFillRectangle(dpy, win, gc, 0, 0, sw, sh);
                /* grey is legible on all eight corners */
                XSetForeground(dpy, gc, pack(vis, 128, 128, 128));
                snprintf(line, sizeof(line),
                         "%s   step %d/8   cycle %d   n=%lld",
                         c->name, step + 1, cycles + 1, n);
                XDrawString(dpy, win, gc, 20, text_y, line,
                            (int)strlen(line));
                XFlush(dpy);
                log_frame(lf, n, c->name);
                step++;
                if (step >= 8)
                {
                    step = 0;
                    cycles++;
                }
                next_due = t + cycle_ms;
            }
            usleep(2000);
            continue;
        }
        /* MODE_SLIDE */
        {
            long long t = now_ms();

            if (next_due <= 0 || t >= next_due)
            {
                n++;
                XSetForeground(dpy, gc, pack(vis, 0, 0, 255));
                XFillRectangle(dpy, win, gc, 0, 0, sw, sh);
                XSetForeground(dpy, gc, pack(vis, 255, 255, 255));
                XFillRectangle(dpy, win, gc, slide_x,
                               (sh - block_h) / 2, block_w, block_h);
                XSetForeground(dpy, gc, pack(vis, 0, 0, 0));
                snprintf(line, sizeof(line),
                         "slide x=%d  step %d px  %d ms  n=%lld",
                         slide_x, slide_px, slide_ms, n);
                XDrawString(dpy, win, gc, slide_x + 8,
                            (sh - block_h) / 2 + text_y, line,
                            (int)strlen(line));
                XFlush(dpy);
                log_frame(lf, n, "slide");
                slide_x += slide_px;
                if (slide_x >= sw)
                {
                    slide_x = -block_w;
                }
                next_due = t + slide_ms;
            }
            usleep(2000);
        }
    }

done:
    if (lf != NULL)
    {
        fclose(lf);
    }
    if (font != NULL)
    {
        XFreeFont(dpy, font);
    }
    XFreeGC(dpy, gc);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
