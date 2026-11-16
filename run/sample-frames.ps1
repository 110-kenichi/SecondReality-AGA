param(
    [string]$OutPrefix = "rb",
    [double]$StartDelaySec = 0,
    [int]$Count = 60,
    [int]$IntervalMs = 100
)
# WinUAE window continuous capture; write host time and file name to CSV.
# capture-hwnd.ps1 waits 0.5s per shot; this one calls PrintWindow without the wait.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class FastCap2 {
  public delegate bool CB(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(CB cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$target = (Get-Process winuae-gdb).Id
$global:capList = New-Object System.Collections.ArrayList
$cb = [FastCap2+CB]{
    param($h, $l)
    $q = 0
    [FastCap2]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
    if ($q -eq $target) {
        $sb = New-Object System.Text.StringBuilder 256
        [FastCap2]::GetWindowText($h, $sb, 256) | Out-Null
        if ($sb.ToString() -eq 'WinUAE') { [void]$global:capList.Add($h) }
    }
    return $true
}
[FastCap2]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
# WinUAE may still be booting when this script starts; retry for 30 s.
for ($try = 0; $try -lt 150 -and $global:capList.Count -eq 0; $try++) {
    Start-Sleep -Milliseconds 200
    [FastCap2]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
}
if ($global:capList.Count -eq 0) { Write-Output "window not found"; exit 1 }
$h = [IntPtr]$global:capList[0]
$r = New-Object FastCap2+RECT
[FastCap2]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$hh = $r.Bottom - $r.Top
Write-Output ("window {0} x {1} at {2},{3}" -f $w, $hh, $r.Left, $r.Top)
if ($w -le 0 -or $hh -le 0) { exit 1 }

$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $StartDelaySec) { Start-Sleep -Milliseconds 50 }
$csv = "$PSScriptRoot\${OutPrefix}_times.csv"
"idx,ms,path" | Set-Content $csv
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
for ($i = 0; $i -lt $Count; $i++) {
    $hdc = $g.GetHdc()
    [FastCap2]::PrintWindow($h, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc)
    $ms = [int]$sw.Elapsed.TotalMilliseconds
    $path = "$PSScriptRoot\{0}{1:d4}.png" -f $OutPrefix, $i
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    "$i,$ms,$path" | Add-Content $csv
    if ($IntervalMs -gt 0) { Start-Sleep -Milliseconds $IntervalMs }
}
$g.Dispose(); $bmp.Dispose()
Write-Output "captured $Count"
