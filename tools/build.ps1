# Build + tests con el entorno MSVC cargado (vcvars64 NO persiste entre
# invocaciones de shell: este script lo recarga siempre). Ejecutar desde la
# raiz del repo:  tools\build.ps1 [-Target release] [-Test] [-VcpkgInstalled <dir>]
# Si el directorio de build no existe o su CMakeCache apunta a otra ruta
# (proyecto movido, worktree nuevo), se reconfigura con el preset.
# -VcpkgInstalled reutiliza paquetes ya instalados (p. ej. desde un worktree).
param([string]$Target = "debug", [switch]$Test, [string]$VcpkgInstalled = "")
$ErrorActionPreference = "Stop"
# Sin diálogos modales de Windows ("no se encontró gtest_main.dll", GPF):
# el modo de error lo heredan ninja, cmake y los tests que lance CTest, así
# que una DLL ausente es un test rojo en consola, no una ventana que bloquea.
# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX.
Add-Type -Namespace Win32 -Name ErrMode -MemberDefinition '[DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint mode);'
[void][Win32.ErrMode]::SetErrorMode(0x8003)
$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vsw -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
}
$ninja = Get-ChildItem "C:\vcpkg\downloads\tools" -Recurse -Filter ninja.exe | Select-Object -First 1
if ($ninja) { $env:PATH += ";$($ninja.DirectoryName)" }
$dir = if ($Target -eq "release") { "cmake-build-release-opengl" } else { "cmake-build-debug-opengl" }
$preset = if ($Target -eq "release") { "Release-OpenGL" } else { "Debug-OpenGL" }

$cache = Join-Path $dir "CMakeCache.txt"
$stale = $true
if (Test-Path $cache) {
    $home_line = Select-String -Path $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$' | Select-Object -First 1
    if ($home_line) {
        $cached = (Resolve-Path -LiteralPath $home_line.Matches[0].Groups[1].Value -ErrorAction SilentlyContinue)
        $stale = -not $cached -or ($cached.Path -ne (Get-Location).Path)
    }
}
if ($stale) {
    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    $extra = @()
    if ($VcpkgInstalled) { $extra += "-DVCPKG_INSTALLED_DIR=$VcpkgInstalled" }
    cmake --preset $preset @extra
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
cmake --build $dir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) { ctest --test-dir $dir --output-on-failure }
exit $LASTEXITCODE
