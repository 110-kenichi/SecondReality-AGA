param(
    [string]$OutPrefix = "px",
    [int]$Count = 1,
    [int]$DelayMs = 500
)
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class FNDP {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
}
"@
$target = (Get-Process winuae-gdb).Id
$script:hw = [IntPtr]::Zero
$cb = [FNDP+EnumWindowsProc]{
    param($h, $l)
    $p = 0
    [FNDP]::GetWindowThreadProcessId($h, [ref]$p) | Out-Null
    if ($p -eq $target) {
        $sb = New-Object System.Text.StringBuilder 128
        [FNDP]::GetWindowText($h, $sb, 128) | Out-Null
        if ($sb.ToString() -eq 'WinUAE') { $script:hw = $h }
    }
    return $true
}
[FNDP]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
for ($i = 0; $i -lt $Count; $i++) {
    & "$PSScriptRoot\capture-hwnd.ps1" -Hwnd $hw.ToInt64() -OutPath ("$PSScriptRoot\{0}{1:d2}.png" -f $OutPrefix, $i) -PrintWindow | Out-Null
    if ($DelayMs -gt 0) { Start-Sleep -Milliseconds $DelayMs }
}
Write-Output "captured $Count"
