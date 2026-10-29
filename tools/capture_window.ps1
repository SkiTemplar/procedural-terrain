# Captura la ventana principal de un proceso a PNG aunque esté tapada por otras
# (PrintWindow con PW_RENDERFULLCONTENT, que compone también OpenGL/DirectX).
# Pensado para verificar capturas con varios agentes lanzando el juego a la vez:
# CopyFromScreen captura lo que haya delante; esto captura SOLO tu proceso.
# Uso: tools\capture_window.ps1 -ProcessId <pid> -Out captura.png
param([Parameter(Mandatory = $true)][int]$ProcessId, [string]$Out = "captura.png")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -Namespace Win32 -Name Capture -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(System.IntPtr hwnd, System.IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetClientRect(System.IntPtr hwnd, out RECT rect);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
'@
$proc = Get-Process -Id $ProcessId
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [System.IntPtr]::Zero) { throw "el proceso $ProcessId no tiene ventana principal" }
$rect = New-Object Win32.Capture+RECT
[void][Win32.Capture]::GetClientRect($hwnd, [ref]$rect)
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { throw "ventana sin área cliente (¿minimizada?)" }
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $gfx.GetHdc()
# 0x3 = PW_CLIENTONLY | PW_RENDERFULLCONTENT
$ok = [Win32.Capture]::PrintWindow($hwnd, $hdc, 3)
$gfx.ReleaseHdc($hdc)
if (-not $ok) { $gfx.Dispose(); $bmp.Dispose(); throw "PrintWindow falló" }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose(); $bmp.Dispose()
Write-Output "captura: $Out"
