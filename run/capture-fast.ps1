param(
    [string]$Title = "WinUAE",
    [string]$OutPrefix = "fast",
    [int]$Count = 12,
    [int]$IntervalMs = 100
)
# 指定タイトルを含むウィンドウを PrintWindow で連続キャプチャする
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class FastCap {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$script:hw = [IntPtr]::Zero
$want = $Title.ToLower()
$cb = [FastCap+EnumWindowsProc]{
    param($h, $l)
    if (-not [FastCap]::IsWindowVisible($h)) { return $true }
    $sb = New-Object System.Text.StringBuilder 256
    [FastCap]::GetWindowText($h, $sb, 256) | Out-Null
    $t = $sb.ToString().ToLower()
    if ($t.Contains($want)) { $script:hw = $h }
    return $true
}
[FastCap]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($hw -eq [IntPtr]::Zero) { throw "window not found: $Title" }
$r = New-Object FastCap+RECT
[FastCap]::GetWindowRect($hw, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$h = $r.Bottom - $r.Top
Add-Type -AssemblyName System.Drawing
for ($i = 0; $i -lt $Count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [FastCap]::PrintWindow($hw, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
    $bmp.Save(("$PSScriptRoot\{0}{1:d2}.png" -f $OutPrefix, $i), [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    if ($i + 1 -lt $Count) { Start-Sleep -Milliseconds $IntervalMs }
}
Write-Output ("captured {0} frames from '{1}' {2}x{3}" -f $Count, $Title, $w, $h)
