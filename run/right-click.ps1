param(
    [long]$Hwnd,
    [int]$Count = 1,
    [int]$HoldMs = 400,
    [int]$GapMs = 800
)
# Send right mouse clicks to an Amiga emulator window (POTINP bit 10 = right button).
Add-Type -AssemblyName System.Windows.Forms
$sig = @'
using System;
using System.Runtime.InteropServices;
public class Win32Mouse {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
  [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
'@
Add-Type -TypeDefinition $sig
$h = [IntPtr]$Hwnd
[Win32Mouse]::ShowWindow($h, 9) | Out-Null
[Win32Mouse]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 400
$r = New-Object Win32Mouse+RECT
[Win32Mouse]::GetClientRect($h, [ref]$r) | Out-Null
$p = New-Object Win32Mouse+POINT
$p.X = [int](($r.Right - $r.Left) / 2)
$p.Y = [int](($r.Bottom - $r.Top) / 2)
[Win32Mouse]::ClientToScreen($h, [ref]$p) | Out-Null
[System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($p.X, $p.Y)
Start-Sleep -Milliseconds 150
for ($i = 0; $i -lt $Count; $i++) {
    [Win32Mouse]::mouse_event(0x0008, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [Win32Mouse]::mouse_event(0x0010, 0, 0, 0, [UIntPtr]::Zero)
    if ($i -lt $Count - 1) { Start-Sleep -Milliseconds $GapMs }
}
Write-Output "right-clicked $Count"
