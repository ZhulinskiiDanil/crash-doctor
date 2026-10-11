# A screenshot of the game's window: brought forward, then copied from the screen
# With -scroll N, the mouse wheel is turned N notches down over the window's middle first
param([string]$out, [int]$scroll = 0)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class GameWindow {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT rect);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, UIntPtr extra);
  public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -AssemblyName System.Drawing

$game = Get-Process GeometryDash -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $game) { Write-Output "no game window"; exit 1 }
[GameWindow]::ShowWindow($game.MainWindowHandle, 9) | Out-Null
[GameWindow]::SetForegroundWindow($game.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 700

$rect = New-Object GameWindow+RECT
[GameWindow]::GetWindowRect($game.MainWindowHandle, [ref]$rect) | Out-Null
if ($scroll -gt 0) {
  [GameWindow]::SetCursorPos([int](($rect.Left + $rect.Right) / 2), [int](($rect.Top + $rect.Bottom) / 2)) | Out-Null
  for ($i = 0; $i -lt $scroll; $i++) {
    # MOUSEEVENTF_WHEEL, one notch down
    [GameWindow]::mouse_event(0x0800, 0, 0, -120, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
  }
  Start-Sleep -Milliseconds 500
}
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
$bitmap.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "screenshot $out ($width x $height)"
