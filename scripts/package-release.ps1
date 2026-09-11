# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force
$buildDir = Join-Path $projectDir 'build/Release'
$releaseDir = Join-Path $projectDir 'release'

$asi = Join-Path $buildDir 'EthanCarterReduxHeadTracking.asi'
if (-not (Test-Path $asi)) {
    throw "Built .asi not found at $asi. Run 'pixi run build' first."
}

# CMakeLists.txt is the canonical version source; release.yml re-reads the
# same project(... VERSION ...) form to check the tag against the artifact.
# Get-ProjectVersion scopes the read to that call, so a three-component
# VERSION on cmake_minimum_required can never be read as the mod version.
$version = Get-ProjectVersion -Source cmake -Path (Join-Path $projectDir 'CMakeLists.txt')

if (Test-Path $releaseDir) { Remove-Item $releaseDir -Recurse -Force }
New-Item -ItemType Directory -Path $releaseDir | Out-Null

# Stage installer ZIP contents in a temp folder
$stage = Join-Path $env:TEMP "ecr-ht-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage | Out-Null

# Plugin payload
$plugins = New-Item -ItemType Directory -Path (Join-Path $stage 'plugins')
Copy-Item -Force $asi (Join-Path $plugins.FullName 'EthanCarterReduxHeadTracking.asi')

# Vendor (loader)
$vendorSrc = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorDst = New-Item -ItemType Directory -Path (Join-Path $stage 'vendor/ultimate-asi-loader')
if (Test-Path $vendorSrc) {
    Copy-Item -Force (Join-Path $vendorSrc '*') $vendorDst.FullName -Recurse
} else {
    Write-Warning 'vendor/ultimate-asi-loader missing - the installer will hard-error at runtime. Run pixi run update-deps.'
}

# Launcher manifest (lopari ingests this). Ship it at the ZIP root and stamp
# the real release version (the committed copy stays at 0.0.0).
$modManifest = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $modManifest)) { throw "launcher-manifest.json not found at $modManifest" }
$manifest = Get-Content -Raw $modManifest | ConvertFrom-Json
$manifest.mod_info.version = $version
# Windows PowerShell 5.1's `-Encoding UTF8` means UTF-8 WITH a BOM, and this is
# the launcher's contract file - the only thing Lopari reads to find out where
# the payload goes. A strict JSON reader rejects a leading BOM outright
# (System.Text.Json: "'0xFEFF' is an invalid start of a value"; JSON.parse the
# same), so the packaged manifest would fail to parse while the repo copy on
# disk parsed fine. Same fix release.ps1 already applies to pixi.toml.
# The trailing newline is explicit: Set-Content used to add one and
# WriteAllText does not, and a file that loses it shows a spurious last-line
# change in every diff of the packaged manifest from here on.
$manifestJson = ($manifest | ConvertTo-Json -Depth 10) + "`r`n"
[System.IO.File]::WriteAllText(
    (Join-Path $stage 'launcher-manifest.json'),
    $manifestJson,
    (New-Object System.Text.UTF8Encoding $false))

# Scripts + find-game shim
Copy-Item -Force (Join-Path $projectDir 'scripts/install.cmd') $stage
Copy-Item -Force (Join-Path $projectDir 'scripts/uninstall.cmd') $stage
# install.cmd and uninstall.cmd are thin wrappers: the body they call lives in
# shared/ at the ZIP root, and without it the installer aborts at its own layout
# check and exits 1 on every run. Copy-SharedBundle stages every body there,
# alongside find-game.ps1, GamePathDetection.psm1 and games.json at the paths
# find-game.ps1 actually looks in.
Copy-SharedBundle -StagingDir $stage

# Docs
foreach ($f in @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')) {
    Copy-Item -Force (Join-Path $projectDir $f) $stage
}

$installerZip = Join-Path $releaseDir "EthanCarterReduxHeadTracking-v$version-installer.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $installerZip -Force
Write-Host "Built $installerZip" -ForegroundColor Green

# Nexus ZIP: only the files that drop into EthanCarter/Binaries/Win64/
$nexusStage = Join-Path $env:TEMP "ecr-ht-nexus-$([Guid]::NewGuid().ToString('N'))"
$nexusInner = New-Item -ItemType Directory -Path (Join-Path $nexusStage 'EthanCarter/Binaries/Win64') -Force
Copy-Item -Force $asi $nexusInner.FullName
$nexusZip = Join-Path $releaseDir "EthanCarterReduxHeadTracking-v$version-nexus.zip"
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStage -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Compress-Archive -Path (Join-Path $nexusStage '*') -DestinationPath $nexusZip -Force
Write-Host "Built $nexusZip" -ForegroundColor Green

Remove-Item $stage -Recurse -Force
Remove-Item $nexusStage -Recurse -Force
