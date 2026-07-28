using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

/* Animated isoluminant chroma test: N frames at CONSTANT Rec.601 luma, hue
   rotating around the chroma circle. Because luma is fixed and only Cb/Cr move,
   every frame differs from the last ONLY in chroma, and the whole surface
   changes each tick -> the server cannot route this to the static PLANAR codec;
   it must encode video (AVC420/AVC444). If chroma survives the wire the client
   shows a smooth hue rotation; if it is mangled it steps or greys out. */
class ChromaAnim : Form
{
    const int N = 60;                 /* frames in the loop           */
    const double Yc = 128.0;          /* fixed luma                   */
    const double Rad = 48.0;          /* chroma radius (in-gamut)     */
    Bitmap[] frames = new Bitmap[N];
    int idx = 0;

    [STAThread]
    static void Main() { Application.EnableVisualStyles(); Application.Run(new ChromaAnim()); }

    ChromaAnim()
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition   = FormStartPosition.Manual;
        Bounds          = Screen.PrimaryScreen.Bounds;
        TopMost = true; BackColor = Color.Black; DoubleBuffered = true;
        Cursor.Hide(); KeyPreview = true;
        KeyDown += (s, e) => { if (e.KeyCode == Keys.Escape) Close(); };
        var kill = new Timer(); kill.Interval = 3600000; kill.Tick += (s, e) => Close(); kill.Start();
        Load += (s, e) => { Build(); var t = new Timer(); t.Interval = 33; t.Tick += Advance; t.Start(); };
    }

    static Color Iso(int k, int phase)
    {
        double th = 2.0 * Math.PI * ((k + phase) % N) / N;
        double cb = Rad * Math.Cos(th), cr = Rad * Math.Sin(th);
        int r = (int)Math.Round(Yc + 1.402 * cr);
        int g = (int)Math.Round(Yc - 0.344136 * cb - 0.714136 * cr);
        int b = (int)Math.Round(Yc + 1.772 * cb);
        return Color.FromArgb(Clamp(r), Clamp(g), Clamp(b));
    }
    static int Clamp(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

    void Build()
    {
        int W = Bounds.Width, H = Bounds.Height, band = H / 4;
        for (int k = 0; k < N; k++)
        {
            var bmp = new Bitmap(W, H, PixelFormat.Format24bppRgb);
            var rc = new Rectangle(0, 0, W, H);
            var bd = bmp.LockBits(rc, ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
            int stride = bd.Stride; byte[] buf = new byte[stride * H];
            Color a = Iso(k, 0), b2 = Iso(k, N / 2);   /* opposite hues, equal luma */
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                {
                    Color c;
                    if (y < band)          c = a;                                   /* flat rotating hue    */
                    else if (y < 2 * band) c = ((x + k) % 2 == 0) ? a : b2;         /* 1px stripes, moving  */
                    else if (y < 3 * band) c = (((x + k) / 2) % 2 == 0) ? a : b2;   /* 2px stripes, moving  */
                    else                   c = ((x + y + k) % 2 == 0) ? a : b2;     /* checkerboard, moving */
                    int o = y * stride + x * 3; buf[o] = c.B; buf[o + 1] = c.G; buf[o + 2] = c.R;
                }
            Marshal.Copy(buf, 0, bd.Scan0, buf.Length); bmp.UnlockBits(bd);
            using (var g = Graphics.FromImage(bmp))
            {
                var yl = new Pen(Color.Yellow, 3);
                g.DrawRectangle(yl, 0, 0, 39, 39); g.DrawRectangle(yl, W - 40, 0, 39, 39);
                g.DrawRectangle(yl, 0, H - 40, 39, 39); g.DrawRectangle(yl, W - 40, H - 40, 39, 39);
                using (var f = new Font("Consolas", 12, FontStyle.Bold))
                using (var wb = new SolidBrush(Color.White))
                using (var bg = new SolidBrush(Color.Black))
                {
                    string s = "ISOLUMINANT anim frame " + k + "/" + N +
                               "  Yconst=128 hueRGB=(" + a.R + "," + a.G + "," + a.B + ")  " + W + "x" + H;
                    g.FillRectangle(bg, 48, 4, 620, 20); g.DrawString(s, f, wb, 50, 4);
                }
            }
            frames[k] = bmp;
        }
    }

    void Advance(object s, EventArgs e) { idx = (idx + 1) % N; Invalidate(); }
    protected override void OnPaint(PaintEventArgs e)
    { if (frames[idx] != null) e.Graphics.DrawImageUnscaled(frames[idx], 0, 0); }
    protected override void OnPaintBackground(PaintEventArgs e) { }
}
