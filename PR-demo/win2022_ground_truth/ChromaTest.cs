using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Windows.Forms;

/* Full-screen GDI isoluminant chroma test pattern.
   Rec.709 isoluminant pair: RED(255,0,0) Y=54.2  CYAN(0,69,69) Y=54.3.
   Under 4:2:0 the pure-chroma edges collapse toward flat gray; under true
   4:4:4 they stay razor sharp. Coverage of the WHOLE surface is the point:
   any region that silently fell back to 420 self-reveals as a blurred band. */
class ChromaForm : Form
{
    Bitmap bmp;
    static readonly Color RED  = Color.FromArgb(255, 0, 0);
    static readonly Color CYAN = Color.FromArgb(0, 69, 69);

    [STAThread]
    static void Main()
    {
        Application.EnableVisualStyles();
        Application.Run(new ChromaForm());
    }

    ChromaForm()
    {
        FormBorderStyle = FormBorderStyle.None;
        StartPosition   = FormStartPosition.Manual;
        Bounds          = Screen.PrimaryScreen.Bounds;
        TopMost         = true;
        BackColor       = Color.Black;
        DoubleBuffered  = true;
        Cursor.Hide();
        KeyPreview = true;
        KeyDown += (s, e) => { if (e.KeyCode == Keys.Escape) Close(); };
        var t = new Timer(); t.Interval = 3600000; t.Tick += (s, e) => Close(); t.Start();
        Load += (s, e) => Build();
    }

    void Build()
    {
        int W = Bounds.Width, H = Bounds.Height;
        bmp = new Bitmap(W, H, PixelFormat.Format24bppRgb);
        var r = new Rectangle(0, 0, W, H);
        var bd = bmp.LockBits(r, ImageLockMode.WriteOnly, PixelFormat.Format24bppRgb);
        int stride = bd.Stride;
        byte[] buf = new byte[stride * H];
        int band = H / 4;
        for (int y = 0; y < H; y++)
        {
            for (int x = 0; x < W; x++)
            {
                Color c;
                if (y < band)               c = (x % 2 == 0) ? RED : Color.Blue;      /* B1 naive R/B 1px */
                else if (y < 2 * band)      c = (x % 2 == 0) ? RED : CYAN;            /* B2 isolum 1px    */
                else if (y < 3 * band)      c = ((x / 2) % 2 == 0) ? RED : CYAN;      /* B3 isolum 2px    */
                else                        c = ((x + y) % 2 == 0) ? RED : CYAN;      /* B4 checkerboard  */
                int o = y * stride + x * 3;
                buf[o] = c.B; buf[o + 1] = c.G; buf[o + 2] = c.R;
            }
        }
        Marshal.Copy(buf, 0, bd.Scan0, buf.Length);
        bmp.UnlockBits(bd);

        using (var g = Graphics.FromImage(bmp))
        {
            var yellow = new Pen(Color.Yellow, 3);
            g.DrawRectangle(yellow, 0, 0, 39, 39);
            g.DrawRectangle(yellow, W - 40, 0, 39, 39);
            g.DrawRectangle(yellow, 0, H - 40, 39, 39);
            g.DrawRectangle(yellow, W - 40, H - 40, 39, 39);
            g.DrawLine(yellow, W / 2 - 30, H / 2, W / 2 + 30, H / 2);
            g.DrawLine(yellow, W / 2, H / 2 - 30, W / 2, H / 2 + 30);
            using (var f = new Font("Consolas", 12, FontStyle.Bold))
            using (var wb = new SolidBrush(Color.White))
            using (var bg = new SolidBrush(Color.Black))
            {
                string[] L = {
                    "B1 naive R/B 1px (luma+chroma)",
                    "B2 ISOLUMINANT R/cyan 1px  <-- 420 -> gray mush",
                    "B3 isoluminant R/cyan 2px",
                    "B4 isoluminant checkerboard 1px  " + W + "x" + H };
                for (int i = 0; i < 4; i++)
                {
                    g.FillRectangle(bg, 48, i * band + 2, 460, 20);
                    g.DrawString(L[i], f, wb, 50, i * band + 2);
                }
            }
        }
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        if (bmp != null) e.Graphics.DrawImageUnscaled(bmp, 0, 0);
    }
    protected override void OnPaintBackground(PaintEventArgs e) { }
}
