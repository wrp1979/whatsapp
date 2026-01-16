param(
    [string]$BuildDir = "build-windows",
    [string]$Config = "release"
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path $root).Path

$buildPath = Join-Path $root $BuildDir
New-Item -ItemType Directory -Force $buildPath | Out-Null

$qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
if (-not $qmake) {
    throw "qmake.exe not found in PATH"
}

Push-Location $buildPath
& $qmake.Source (Join-Path $root "src\\WhatsApp.pro") "CONFIG+=$Config" "QMAKE_CXXFLAGS+=-Zc:__cplusplus" "QMAKE_CXXFLAGS_RELEASE+=-Zc:__cplusplus" "QMAKE_CXXFLAGS+=-permissive-" "QMAKE_CXXFLAGS_RELEASE+=-permissive-"
& nmake
Pop-Location
