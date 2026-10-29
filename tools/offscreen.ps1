# Lanza ProceduralTerrain en un modo fuera de pantalla (--shots, --record o
# --bench: ventana GLFW oculta) con prioridad BelowNormal, y comprueba que la
# ventana en primer plano no cambia mientras corre. Si cambia, mata el proceso
# lanzado (por PID) y sale con 3.
# Uso: tools\offscreen.ps1 -Exe <ruta.exe> -Log <fichero.log> -- <args del exe>
param(
    [Parameter(Mandatory)] [string]$Exe,
    [Parameter(Mandatory)] [string]$Log,
    [Parameter(ValueFromRemainingArguments)] [string[]]$ExeArgs
)
$ErrorActionPreference = "Stop"
Add-Type -Namespace Win32 -Name Fg -MemberDefinition @'
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr h, out uint pid);
'@
function Get-ForegroundPid {
    $procId = [uint32]0
    [void][Win32.Fg]::GetWindowThreadProcessId([Win32.Fg]::GetForegroundWindow(), [ref]$procId)
    return $procId
}
$quoted = $ExeArgs | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
$before = Get-ForegroundPid
$proc = Start-Process -FilePath $Exe -ArgumentList ($quoted -join ' ') -NoNewWindow -PassThru `
    -RedirectStandardOutput $Log -RedirectStandardError "$Log.err"
$null = $proc.Handle  # sin esto ExitCode queda vacío con -NoNewWindow
$proc.PriorityClass = 'BelowNormal'
while (-not $proc.HasExited) {
    $now = Get-ForegroundPid
    if ($now -eq $proc.Id) {
        Stop-Process -Id $proc.Id -Force
        Write-Error "el proceso offscreen tomo el primer plano: abortado"
        exit 3
    }
    Start-Sleep -Milliseconds 250
}
$proc.WaitForExit()
$after = Get-ForegroundPid
"primer plano antes=$before despues=$after; codigo=$($proc.ExitCode)"
exit $proc.ExitCode
