using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Windows.Forms;

/* Scrolling saturated color-band payload. Unlike the isoluminant animation
   (chroma-only -> mostly LC=2 aux), this scrolls sharp saturated color bars AND
   varies luma, so each frame changes both the luma (main) and the chroma detail
   across the surface. Intent: provoke the server's combined main+aux path
   (AVC444 LC=0) as often as possible so we can sample real-Windows LC=0. */
class ChromaScroll : Form
{
    int t = 0;
    Timer tm;
    static readonly Color[] Bars = {
        Color.FromArgb(255,0,0), Color.FromArgb(0,255,0), Color.FromArgb(0,0,255),
        Color.FromArgb(255,255,0), Color.FromArgb(0,255,255), Color.FromArgb(255,0,255),
        Color.FromArgb(255,128,0), Color.FromArgb(128,0,255)
    };

    [STAThread]
    static void Main() { Application.EnableVisualStyles(); Application.Run(new ChromaScroll()); }

    ChromaScroll()
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition   = FormStartPosition.Manual;
        Bounds          = Screen.PrimaryScreen.Bounds;
        TopMost = true; BackColor = Color.Black; DoubleBuffered = true;
        Cursor.Hide(); KeyPreview = true;
        KeyDown += (s,e) => { if (e.KeyCode==Keys.Escape) Application.Exit(); };
        tm = new Timer(); tm.Interval = 33; tm.Tick += (s,e) => { t++; Invalidate(); };
        tm.Start();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        int W = ClientSize.Width, H = ClientSize.Height;
        Graphics g = e.Graphics;
        int bw = 96;                       /* bar width */
        int off = (t * 6) % bw;            /* horizontal scroll */
        /* vertical saturated color bars scrolling left */
        for (int x = -bw; x < W + bw; x += bw)
        {
            int bi = (((x + t * 6) / bw) % Bars.Length + Bars.Length) % Bars.Length;
            using (var b = new SolidBrush(Bars[bi]))
                g.FillRectangle(b, x - off, 0, bw, H);
        }
        /* a moving luma-varying diagonal white wedge (forces luma-plane motion) */
        int wy = (t * 11) % (H + 200) - 100;
        using (var wb = new SolidBrush(Color.FromArgb(180, 255, 255, 255)))
            g.FillRectangle(wb, 0, wy, W, 60);
        /* saturated colored text (subpixel chroma detail) scrolling */
        string msg = "AVC444 CHROMA SCROLL  " + t;
        using (var f = new Font("Consolas", 40, FontStyle.Bold))
        {
            int tx = W - (t * 9) % (W + 900);
            g.DrawString(msg, f, Brushes.Yellow, tx, H/2 - 120);
            g.DrawString(msg, f, Brushes.Cyan,   tx + 200, H/2 + 40);
            g.DrawString(msg, f, Brushes.Magenta,tx - 150, H/2 + 200);
        }
    }
}
