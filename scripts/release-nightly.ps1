# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$cmakeLists = Join-Path $ProjectRoot 'CMakeLists.txt'
$versionMatch = Select-String -Path $cmakeLists -Pattern 'project\(EthanCarterReduxHeadTracking VERSION ([0-9]+\.[0-9]+\.[0-9]+)'
if (-not $versionMatch) {
    throw "Could not extract version from $cmakeLists"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'the-vanishing-of-ethan-carter-redux' `
    -ModName 'EthanCarterReduxHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
