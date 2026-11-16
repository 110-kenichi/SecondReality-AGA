param([long]$Hwnd)
# 指定ウィンドウに WM_CLOSE を送る (メッセージボックスを閉じる)
$sig = @'
using System;
using System.Runtime.InteropServices;
public class Win32Close {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
'@
Add-Type -TypeDefinition $sig
$h = [IntPtr]$Hwnd
[Win32Close]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Write-Output "WM_CLOSE sent"
