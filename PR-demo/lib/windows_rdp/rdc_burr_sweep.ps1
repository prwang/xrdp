<#
  rdc_burr_sweep.ps1 — resize an RDCMan/mstsc window across a width range and
  screenshot each step, to correlate the AVC444 resize "comb" burr with width.

  Run on the WINDOWS client (where RDCMan.exe / mstsc.exe runs), in PowerShell.
  The RDP session must already be connected and visible (not minimized).

  Examples:
    # sweep window width 1400..1560 in 8px steps, default height, into .\burr
    .\rdc_burr_sweep.ps1 -Proc RDCMan -Min 1400 -Max 1560 -Step 8

    # fine sweep to find the exact transition (2px steps), taller settle
    .\rdc_burr_sweep.ps1 -Proc RDCMan -Min 2170 -Max 2210 -Step 2 -SettleMs 1200

    # mstsc instead of RDCMan
    .\rdc_burr_sweep.ps1 -Proc mstsc -Min 1400 -Max 1560 -Step 16

  Output: <OutDir>\w<width>.png per step, plus an index.csv with the predicted
  parity so you can eyeball / sort. Screenshots capture the window's CLIENT area
  (the RDP content, no title bar).
#>
param(
    [string]$Proc      = "RDCMan",   # process name w/o .exe (RDCMan or mstsc)
    [int]   $Min       = 1400,       # first WINDOW width to try
    [int]   $Max       = 1560,       # last WINDOW width
    [int]   $Step      = 8,          # width increment
    [int]   $Height    = 0,          # 0 = keep current window height
    [int]   $SettleMs  = 900,        # wait after resize for session to redraw
    [string]$OutDir    = ".\burr"
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
}
"@
Add-Type -AssemblyName System.Drawing

$SWP_NOMOVE = 0x2; $SWP_NOZORDER = 0x4; $SWP_NOACTIVATE = 0x10
$HWND_TOP = [IntPtr]::Zero

$p = Get-Process -Name $Proc -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Error "No visible window for process '$Proc'. Is the RDP session connected and not minimized?"; exit 1 }
$h = $p.MainWindowHandle
Write-Host "Target: $Proc  hwnd=$h  ('$($p.MainWindowTitle)')"

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$rows = @()

# current window rect (to preserve height/pos if not overridden)
$wr = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$wr)
$curH = $wr.bottom - $wr.top
if ($Height -le 0) { $Height = $curH }

[W]::ShowWindow($h, 9) | Out-Null   # SW_RESTORE (un-maximize/minimize)
Start-Sleep -Milliseconds 300

for ($w = $Min; $w -le $Max; $w += $Step) {
    # resize (keep position), bring to front, let the session resize + redraw
    [void][W]::SetWindowPos($h, $HWND_TOP, 0, 0, $w, $Height, ($SWP_NOMOVE -bor $SWP_NOZORDER))
    [void][W]::SetForegroundWindow($h)
    Start-Sleep -Milliseconds $SettleMs

    # capture the CLIENT area (RDP content) from screen
    $cr = New-Object W+RECT; [void][W]::GetClientRect($h, [ref]$cr)
    $cw = $cr.right - $cr.left; $chh = $cr.bottom - $cr.top
    $org = New-Object W+POINT; $org.x = 0; $org.y = 0
    [void][W]::ClientToScreen($h, [ref]$org)

    $bmp = New-Object System.Drawing.Bitmap($cw, $chh)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($org.x, $org.y, 0, 0, (New-Object System.Drawing.Size($cw, $chh)))
    $g.Dispose()
    $file = Join-Path $OutDir ("w{0}_client{1}x{2}.png" -f $w, $cw, $chh)
    $bmp.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()

    $predict = [int]([math]::Floor($cw / 16)) % 2   # your heuristic on CLIENT width
    $rows += [pscustomobject]@{ WinWidth=$w; ClientW=$cw; ClientH=$chh; MBcols=[int][math]::Floor($cw/16); PredictBurr=$predict; File=(Split-Path $file -Leaf) }
    Write-Host ("w={0,5}  client={1,5}x{2}  mbcols={3}  predictBurr={4}" -f $w, $cw, $chh, [int][math]::Floor($cw/16), $predict)
}

$rows | Export-Csv -Path (Join-Path $OutDir "index.csv") -NoTypeInformation
Write-Host "`nDone. $($rows.Count) shots in $OutDir  (see index.csv). Zip and send back the ones that burr."
