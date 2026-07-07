# Build + tests con el entorno MSVC cargado (vcvars64 NO persiste entre
# invocaciones de shell: este script lo recarga siempre). Ejecutar desde la
# raiz del repo:  tools\build.ps1 [-Target release] [-Test]
param([string]$Target = "debug", [switch]$Test)
$ErrorActionPreference = "Stop"
$vsw = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vsw -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2]) }
}
$ninja = Get-ChildItem "C:\vcpkg\downloads\tools" -Recurse -Filter ninja.exe | Select-Object -First 1
if ($ninja) { $env:PATH += ";$($ninja.DirectoryName)" }
$dir = if ($Target -eq "release") { "cmake-build-release-opengl" } else { "cmake-build-debug-opengl" }
cmake --build $dir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Test) { ctest --test-dir $dir --output-on-failure }
exit $LASTEXITCODE
