# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

[CmdletBinding()]
param([Parameter(Position = 0)][string]$GamePath)
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $root 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

$asi = Join-Path $root 'build/Release/EthanCarterReduxHeadTracking.asi'
if (-not (Test-Path $asi)) {
    Write-Host "ERROR: build output not found at $asi. Run 'pixi run build' first." -ForegroundColor Red
    exit 1
}

# Same resolution order install.cmd uses: an explicitly supplied path wins,
# otherwise Find-GamePath walks env var -> registry -> Steam libraries from the
# games.json entry.
if ($GamePath) {
    if (-not (Test-Path -LiteralPath $GamePath -PathType Container)) {
        Write-Host "ERROR: supplied game path is not a directory: $GamePath" -ForegroundColor Red
        exit 1
    }
} else {
    $GamePath = Find-GamePath -GameId 'ethan-carter-redux'
}
if (-not $GamePath) {
    Write-Host 'ERROR: The Vanishing of Ethan Carter Redux install not found. Set ETHAN_CARTER_REDUX_PATH or pass the path as the first argument.' -ForegroundColor Red
    exit 1
}

$cfg = Get-GameConfig -GameId 'ethan-carter-redux'
$exe = Join-Path $GamePath $cfg.Executable
if (-not (Test-Path $exe)) {
    Write-Host "ERROR: game exe not found at $exe." -ForegroundColor Red
    exit 1
}
$exeDir = Split-Path $exe

Write-Host "Deploying to $exeDir" -ForegroundColor Cyan

# ASI_LOADER_NAME in install.cmd is winmm.dll; the vendored artifact ships as
# dinput8.dll and is renamed on deploy, exactly as the installer does it. The
# game already has UE4SS proxying dwmapi.dll, so the loader takes winmm.dll -
# another static import of the shipping exe that nothing else has claimed.
$loader = Join-Path $exeDir 'winmm.dll'
if (-not (Test-Path $loader)) {
    $vendorLoader = Join-Path $root 'vendor/ultimate-asi-loader/dinput8.dll'
    if (-not (Test-Path $vendorLoader)) {
        Write-Host "ERROR: vendored loader missing at $vendorLoader. Run 'pixi run update-deps'." -ForegroundColor Red
        exit 1
    }
    Copy-Item $vendorLoader $loader -Force
    Write-Host '  Deployed Ultimate ASI Loader -> winmm.dll' -ForegroundColor Green
}

Copy-Item $asi (Join-Path $exeDir 'EthanCarterReduxHeadTracking.asi') -Force
Write-Host "Deployed EthanCarterReduxHeadTracking.asi to $exeDir" -ForegroundColor Green
