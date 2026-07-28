/* Animated isoluminant 1px-stripe chroma test (X11/Xlib).
 *
 * Bands (top->bottom), all scrolling 1px/frame so the surface changes every
 * frame and xrdp routes it through the AVC444 video path (a STATIC image would
 * route to the lossless PLANAR codec and prove nothing about 4:4:4):
 *   B1  naive RED/BLUE 1px      (luma+chroma edge, survives even 4:2:0)
 *   B2  isoluminant RED/CYAN 1px  <-- THE test: 4:2:0 averages it to grey,
 *                                     4:4:4 keeps sharp red/cyan
 *   B3  isoluminant RED/CYAN 2px
 *   B4  isoluminant checkerboard 1px (worst case for chroma)
 * RED(255,0,0) and CYAN(0,69,69) are Rec.709 isoluminant (Y~=54), so any
 * luminance edge you see in B2/B3/B4 is pure chroma detail.
 *
 * build: gcc -O2 chroma_strip_anim.c -lX11 -o chroma_strip_anim
 * run:   DISPLAY=:10 XAUTHORITY=/var/run/xrdp/1000/Xauthority ./chroma_strip_anim [seconds] [fps]
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static int mask_shift(unsigned long m)
{
    int s = 0;
    if (m == 0)
    {
        return 0;
    }
    while (((m >> s) & 1) == 0)
    {
        s++;
    }
    return s;
}

int main(int argc, char **argv)
{
    Display *dpy;
    int scr, w, h, depth, seconds = 120, fps = 30, rs, gs, bs;
    unsigned long RED, CYAN, BLUE;
    Window win, root;
    Visual *vis;
    XImage *img;
    XSetWindowAttributes swa;
    char *data;
    long frame = 0;

    if (argc > 1)
    {
        seconds = atoi(argv[1]);
    }
    if (argc > 2)
    {
        fps = atoi(argv[2]);
    }
    if (fps < 1)
    {
        fps = 1;
    }
    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        fprintf(stderr, "cannot open display\n");
        return 1;
    }
    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    w = DisplayWidth(dpy, scr);
    h = DisplayHeight(dpy, scr);
    depth = DefaultDepth(dpy, scr);
    vis = DefaultVisual(dpy, scr);
    rs = mask_shift(vis->red_mask);
    gs = mask_shift(vis->green_mask);
    bs = mask_shift(vis->blue_mask);
    RED  = (255UL << rs) | (0UL   << gs) | (0UL  << bs);
    CYAN = (0UL   << rs) | (69UL  << gs) | (69UL << bs);
    BLUE = (0UL   << rs) | (0UL   << gs) | (255UL << bs);

    swa.override_redirect = True;
    swa.background_pixel = BlackPixel(dpy, scr);
    swa.event_mask = ButtonPressMask | KeyPressMask;
    win = XCreateWindow(dpy, root, 0, 0, w, h, 0, depth, InputOutput, vis,
                        CWOverrideRedirect | CWBackPixel | CWEventMask, &swa);
    XMapRaised(dpy, win);
    XSetInputFocus(dpy, win, RevertToParent, CurrentTime);

    data = malloc((size_t)w * h * 4);
    if (data == NULL)
    {
        return 1;
    }
    img = XCreateImage(dpy, vis, depth, ZPixmap, 0, data, w, h, 32, 0);
    if (img == NULL)
    {
        return 1;
    }

    while (frame < (long)seconds * fps)
    {
        int x, y;
        int b1 = h / 4, b2 = h / 2, b3 = 3 * h / 4;
        int ph = (int)(frame % 2);        /* 1px scroll for the 1px bands   */
        int ph2 = (int)(frame % 4);       /* smooth scroll for the 2px band */
        for (y = 0; y < h; y++)
        {
            for (x = 0; x < w; x++)
            {
                unsigned long c;
                int xp = x + ph;
                if (y < b1)
                {
                    c = (xp & 1) ? RED : BLUE;
                }
                else if (y < b2)
                {
                    c = (xp & 1) ? RED : CYAN;
                }
                else if (y < b3)
                {
                    c = (((x + ph2) / 2) & 1) ? RED : CYAN;
                }
                else
                {
                    c = ((x + y + ph) & 1) ? RED : CYAN;
                }
                XPutPixel(img, x, y, c);
            }
        }
        /* yellow corner markers to confirm full-surface coverage */
        {
            unsigned long Y = RED | CYAN | BLUE; /* not exact yellow, but bright */
            int i, j;
            (void)Y;
            for (i = 0; i < 40; i++)
            {
                for (j = 0; j < 40; j++)
                {
                    unsigned long yel = (255UL << rs) | (255UL << gs) | (0UL << bs);
                    XPutPixel(img, i, j, yel);
                    XPutPixel(img, w - 1 - i, j, yel);
                    XPutPixel(img, i, h - 1 - j, yel);
                    XPutPixel(img, w - 1 - i, h - 1 - j, yel);
                }
            }
        }
        XPutImage(dpy, win, DefaultGC(dpy, scr), img, 0, 0, 0, 0, w, h);
        XFlush(dpy);
        while (XPending(dpy))
        {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == KeyPress || ev.type == ButtonPress)
            {
                goto done;
            }
        }
        usleep(1000000 / fps);
        frame++;
    }
done:
    XDestroyImage(img);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    return 0;
}
