param([string]$OutPath = "run\srpp.png")
# Capture the SecondReality++ window with PrintWindow flag=2.
# Do not maximize the window.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class CapSR {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
$proc = Get-Process | Where-Object { $_.ProcessName -like "*SecondReality*" } | Select-Object -First 1
if (-not $proc) { throw "SecondReality++ process not found" }
$target = 0
$script:best = [IntPtr]::Zero
$script:area = 0
$cb = [CapSR+EnumProc]{
    param($h, $l)
    $p2 = 0
    [CapSR]::GetWindowThreadProcessId($h, [ref]$p2) | Out-Null
    if ($p2 -eq $proc.Id) {
        $r = New-Object CapSR+RECT
        [CapSR]::GetWindowRect($h, [ref]$r) | Out-Null
        $a = ($r.R - $r.L) * ($r.B - $r.T)
        if ($a -gt $script:area) { $script:area = $a; $script:best = $h }
    }
    return $true
}
[CapSR]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
$target = $script:best
if ($target -eq [IntPtr]::Zero) { throw "window not found" }
$wr = New-Object CapSR+RECT
[CapSR]::GetWindowRect($target, [ref]$wr) | Out-Null
$w = $wr.R - $wr.L
$hh = $wr.B - $wr.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[CapSR]::PrintWindow($target, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("saved {0} ({1}x{2})" -f $OutPath, $w, $hh)
