param([string]$OutPath = "")
# WinUAE の子ウィンドウ (AmigaPowah) を PrintWindow flag=2 でキャプチャする
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class CapChild {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
$target = (Get-Process winuae-gdb -ErrorAction SilentlyContinue | Select-Object -First 1).Id
if (-not $target) { throw "winuae-gdb process not found" }

$script:found = @()
$cb = [CapChild+EnumProc]{
    param($h, $l)
    $p = 0
    [CapChild]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
    if ($p -eq $target) {
        $sb = New-Object System.Text.StringBuilder 256
        [CapChild]::GetWindowText($h, $sb, 256) | Out-Null
        $cn = New-Object System.Text.StringBuilder 256
        [CapChild]::GetClassName($h, $cn, 256) | Out-Null
        $r = New-Object CapChild+RECT
        [CapChild]::GetWindowRect($h, [ref]$r) | Out-Null
        $script:found += [pscustomobject]@{ Hwnd = $h; Title = $sb.ToString(); Class = $cn.ToString(); W = ($r.Right - $r.Left); H = ($r.Bottom - $r.Top) }
    }
    return $true
}
[CapChild]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
# 子も列挙
$tops = $script:found | ForEach-Object { $_.Hwnd }
foreach ($t in $tops) { [CapChild]::EnumChildWindows($t, $cb, [IntPtr]::Zero) | Out-Null }

$script:found | ForEach-Object { "hwnd=0x{0:X} {1}x{2} class='{3}' title='{4}'" -f $_.Hwnd.ToInt64(), $_.W, $_.H, $_.Class, $_.Title }

$target2 = $script:found | Where-Object { $_.Class -eq 'AmigaPowah' -and $_.W -gt 100 } | Select-Object -First 1
if (-not $target2) { throw "AmigaPowah child window not found" }
if ($OutPath -eq "") { $OutPath = "run\child.png" }
$w = $target2.W; $h = $target2.H
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[CapChild]::PrintWindow($target2.Hwnd, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$g.Dispose()
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output ("saved {0} ({1}x{2})" -f $OutPath, $w, $h)
