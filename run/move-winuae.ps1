param([int]$ProcessId = 0, [int]$WaitSeconds = 0, [int]$Margin = 0, [string]$Mode = "titlebar")
# Move the WinUAE window near the bottom of the primary screen (above taskbar).
#   Mode "titlebar": only the title bar stays visible, window content is below the screen.
#   Mode "bottom":   the whole window is visible, bottom-aligned.
# Uses SetWindowPos with SWP_ASYNCWINDOWPOS so it never blocks on WinUAE's message loop.
$sig = @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class WinMove {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
  [DllImport("user32.dll")] public static extern bool SystemParametersInfo(uint uiAction, uint uiParam, out RECT pvParam, uint fWinIni);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
  public const uint SPI_GETWORKAREA = 0x0030;
  public const uint SWP_NOZORDER = 0x0004;
  public const uint SWP_NOACTIVATE = 0x0010;
  public const uint SWP_ASYNCWINDOWPOS = 0x4000;
  public static RECT WorkArea() {
    RECT r;
    SystemParametersInfo(SPI_GETWORKAREA, 0, out r, 0);
    return r;
  }
  public static IntPtr FindMain(uint pid) {
    IntPtr best = IntPtr.Zero; long bestArea = 0;
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        RECT r; GetWindowRect(h, out r);
        long w = r.Right - r.Left, hh = r.Bottom - r.Top;
        if (w > 100 && hh > 100 && w * hh > bestArea) { bestArea = w * hh; best = h; }
      }
      return true;
    }, IntPtr.Zero);
    return best;
  }
}
'@
Add-Type -TypeDefinition $sig

function Get-WinUAEProcess {
    if ($ProcessId -ne 0) { return Get-Process -Id $ProcessId -ErrorAction SilentlyContinue }
    return Get-Process | Where-Object { $_.ProcessName -like "*winuae*" } | Select-Object -First 1
}

$deadline = (Get-Date).AddSeconds($WaitSeconds)
while ($true) {
    $proc = Get-WinUAEProcess
    if ($proc) {
        $hwnd = [WinMove]::FindMain([uint32]$proc.Id)
        if ($hwnd -ne [IntPtr]::Zero) {
            $wa = [WinMove]::WorkArea()
            $r = New-Object WinMove+RECT
            $cr = New-Object WinMove+RECT
            [WinMove]::GetWindowRect($hwnd, [ref]$r) | Out-Null
            [WinMove]::GetClientRect($hwnd, [ref]$cr) | Out-Null
            $w = $r.Right - $r.Left
            $h = $r.Bottom - $r.Top
            $ncHeight = $h - ($cr.Bottom - $cr.Top)
            if ($Mode -eq "titlebar") {
                $y = $wa.Bottom - $ncHeight - $Margin
            } else {
                $y = $wa.Bottom - $h - $Margin
            }
            if ($y -lt $wa.Top) { $y = $wa.Top }
            $x = $r.Left
            if ($x + $w -gt $wa.Right) { $x = $wa.Right - $w }
            if ($x -lt $wa.Left) { $x = $wa.Left }
            $flags = [WinMove]::SWP_NOZORDER -bor [WinMove]::SWP_NOACTIVATE -bor [WinMove]::SWP_ASYNCWINDOWPOS
            [WinMove]::SetWindowPos($hwnd, [IntPtr]::Zero, $x, $y, $w, $h, $flags) | Out-Null
            # wait briefly for the async move to be applied
            $ok = $false
            for ($i = 0; $i -lt 20; $i++) {
                Start-Sleep -Milliseconds 250
                $r2 = New-Object WinMove+RECT
                [WinMove]::GetWindowRect($hwnd, [ref]$r2) | Out-Null
                if ($r2.Top -eq $y -and $r2.Left -eq $x) { $ok = $true; break }
            }
            Write-Output ("moved winuae window hwnd=0x{0:X} mode={1} to ({2},{3}) size {4}x{5} ncHeight={6} applied={7} (workarea {8},{9}-{10},{11})" -f $hwnd.ToInt64(), $Mode, $x, $y, $w, $h, $ncHeight, $ok, $wa.Left, $wa.Top, $wa.Right, $wa.Bottom)
            exit 0
        }
    }
    if ((Get-Date) -ge $deadline) {
        Write-Output "winuae window not found (timeout)"
        exit 1
    }
    Start-Sleep -Seconds 2
}
