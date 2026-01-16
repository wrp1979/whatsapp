param(
    [string]$BuildDir = "build-windows",
    [string]$OutDir = "dist/windows",
    [string]$DeployDir = "",
    [string]$Version = "",
    [switch]$SkipDeploy
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = (Resolve-Path $root).Path

$buildPath = Join-Path $root $BuildDir
if (-not (Test-Path $buildPath)) {
    throw "Build directory not found: $buildPath"
}

$exeCandidates = @(
    (Join-Path $buildPath "whatsie.exe"),
    (Join-Path $buildPath "release\\whatsie.exe"),
    (Join-Path $buildPath "bin\\whatsie.exe")
)

$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exePath) {
    $exeFound = Get-ChildItem -Path $buildPath -Recurse -Filter "whatsie.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exeFound) {
        $exePath = $exeFound.FullName
    }
}
if (-not $exePath) {
    throw "whatsie.exe not found under $buildPath"
}

if ([string]::IsNullOrWhiteSpace($DeployDir)) {
    $DeployDir = Join-Path $buildPath "deploy"
} else {
    $DeployDir = Join-Path $root $DeployDir
}

if (-not $SkipDeploy) {
    if (Test-Path $DeployDir) {
        Remove-Item -Recurse -Force $DeployDir
    }
    New-Item -ItemType Directory -Force $DeployDir | Out-Null

    $windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if (-not $windeployqt) {
        throw "windeployqt.exe not found in PATH"
    }

    Copy-Item $exePath $DeployDir
    & $windeployqt.Source --release --no-translations (Join-Path $DeployDir "whatsie.exe")
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $proPath = Join-Path $root "src\\WhatsApp.pro"
    $proContent = Get-Content $proPath -Raw
    if ($proContent -match "(?m)^\\s*VERSION\\s*=\\s*([^\\r\\n]+)") {
        $version = $Matches[1].Trim()
    } else {
        throw "VERSION not found in src/WhatsApp.pro"
    }
} else {
    $version = $Version
}

$buildNum = "0"
$buildNumFile = Join-Path $root "src\\BUILD_NUMBER"
if (Test-Path $buildNumFile) {
    $buildNum = (Get-Content $buildNumFile | Select-Object -First 1).Trim()
}

$versionParts = $version.Split(".")
while ($versionParts.Count -lt 3) {
    $versionParts += "0"
}
$versionNsis = "$($versionParts[0]).$($versionParts[1]).$($versionParts[2]).$buildNum"

$outPath = Join-Path $root $OutDir
New-Item -ItemType Directory -Force $outPath | Out-Null

$outFile = Join-Path $outPath ("WhatSie-{0}.{1}-setup.exe" -f $version, $buildNum)

$nsis = Get-Command makensis.exe -ErrorAction SilentlyContinue
if (-not $nsis) {
    throw "makensis.exe not found (install NSIS)"
}

$nsisScript = Join-Path $root "packaging\\windows\\whatsie.nsi"
if (-not (Test-Path $nsisScript)) {
    throw "NSIS script not found: $nsisScript"
}

& $nsis.Source `
    "/DPRODUCT_NAME=WhatSie" `
    "/DPRODUCT_EXE=whatsie.exe" `
    "/DPRODUCT_VERSION=$versionNsis" `
    "/DPRODUCT_PUBLISHER=WhatSie" `
    "/DSOURCE_DIR=$DeployDir" `
    "/DOUT_FILE=$outFile" `
    $nsisScript

Write-Host "Created installer: $outFile"
