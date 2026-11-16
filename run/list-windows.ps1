param([int]$ProcessId)
# 指定プロセスのトップレベルウィンドウを列挙して表示する
$sig = @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WinEnum {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
  [DllImport("user32.dll")] public static extern int GetWindowTextW(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
  public static List<string> List(uint pid) {
    var result = new List<string>();
    EnumWindows((h, l) => {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid) {
        var sb = new StringBuilder(512);
        GetWindowTextW(h, sb, sb.Capacity);
        RECT r; GetWindowRect(h, out r);
        result.Add(string.Format("hwnd=0x{0:X} visible={1} rect=({2},{3})-({4},{5}) title={6}",
          h.ToInt64(), IsWindowVisible(h), r.Left, r.Top, r.Right, r.Bottom, sb.ToString()));
      }
      return true;
    }, IntPtr.Zero);
    return result;
  }
}
'@
Add-Type -TypeDefinition $sig
[WinEnum]::List([uint32]$ProcessId) | ForEach-Object { Write-Output $_ }
