param([long]$Hwnd, [string]$OutPath, [switch]$PrintWindow)
# 指定 HWND のウィンドウをキャプチャする
# 既定: デスクトップ DC からの BitBlt (DirectDraw サーフェスもキャプチャ可能)
# -PrintWindow: PrintWindow API を使用 (D3D 用)
Add-Type -AssemblyName System.Drawing
$sig = @'
using System;
using System.Runtime.InteropServices;
public class Win32Cap3 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);
  [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr hWnd);
  [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int w, int h, IntPtr hdcSrc, int xSrc, int ySrc, int rop);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
'@
Add-Type -TypeDefinition $sig
$h = [IntPtr]$Hwnd
[Win32Cap3]::ShowWindow($h, 9) | Out-Null
[Win32Cap3]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
$r = New-Object Win32Cap3+RECT
[Win32Cap3]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
if ($w -le 0 -or $hh -le 0) { Write-Output "bad rect $w x $hh"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
if ($PrintWindow) {
  [Win32Cap3]::PrintWindow($h, $hdc, 2) | Out-Null
} else {
  # ウィンドウ DC から直接 BitBlt (DirectDraw サーフェス対応・座標ずれなし)
  $src = [Win32Cap3]::GetWindowDC($h)
  [Win32Cap3]::BitBlt($hdc, 0, 0, $w, $hh, $src, 0, 0, 0x00CC0020) | Out-Null
  [Win32Cap3]::ReleaseDC($h, $src) | Out-Null
}
$g.ReleaseHdc($hdc)
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output "saved $OutPath ($w x $hh)"
