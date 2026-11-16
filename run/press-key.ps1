param([long]$Hwnd, [string]$Keys = "{ENTER}")
# 指定ウィンドウを前面にしてキーを送る
Add-Type -AssemblyName System.Windows.Forms
$sig = @'
using System;
using System.Runtime.InteropServices;
public class Win32Key {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
}
'@
Add-Type -TypeDefinition $sig
$h = [IntPtr]$Hwnd
[Win32Key]::ShowWindow($h, 9) | Out-Null
[Win32Key]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500
[System.Windows.Forms.SendKeys]::SendWait($Keys)
Write-Output "sent $Keys"
