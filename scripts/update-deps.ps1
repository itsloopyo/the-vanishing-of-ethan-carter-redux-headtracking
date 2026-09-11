#!/usr/bin/env pwsh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range. Manual; commit the result. CI never refreshes;
# install.cmd extracts the committed vendor tree.
#
# Special case: Ultimate-ASI-Loader ships a DLL inside a release zip, not as a
# standalone asset, so this script extracts dinput8.dll rather than calling
# Update-VendoredLoader, which vendors the downloaded artifact whole.

# -AcceptNewUpstream adopts a binary whose hash is not the pinned one. It is the
# deliberate act that a supply-chain pin exists to require; see $PinnedDllSha256.
param([switch]$AcceptNewUpstream)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

# SHA-256 of the dinput8.dll this repo currently vendors.
#
# This is the highest-trust artifact the repo redistributes: install.cmd copies
# it to winmm.dll next to the game exe, and the game loads it into its own
# process on every launch. Hashing the download AFTER the fact and writing that
# hash into the vendor README records whatever arrived rather than checking it,
# so a compromised release asset or a MITM would land in the tree looking
# exactly like an ordinary version bump - a binary blob diff and a changed hash
# line. The same reasoning as the MinHook commit pin in CMakeLists.txt, and it
# applies harder here, because this one is loaded rather than compiled.
#
# To take a genuine upstream release: verify it, re-run with -AcceptNewUpstream,
# then paste the hash it prints into this line in the same commit.
$PinnedDllSha256 = 'fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'git submodule update --init --recursive'."
}
Import-Module $module -Force

$vendorAsiDir     = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll     = Join-Path $vendorAsiDir 'dinput8.dll'
$vendorAsiLicense = Join-Path $vendorAsiDir 'LICENSE'
$vendorAsiReadme  = Join-Path $vendorAsiDir 'README.md'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

$tempDir = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$tempZip     = Join-Path $tempDir 'upstream.zip'
$tempDll     = Join-Path $tempDir 'dinput8.dll'
$tempLicense = Join-Path $tempDir 'LICENSE'
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($dllEntry, $tempDll, $true)

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($licenseEntry, $tempLicense, $true)
        }
    } finally { $zip.Dispose() }

    $dllSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

    if ($dllSha -ne $PinnedDllSha256) {
        if (-not $AcceptNewUpstream) {
            throw @"
Upstream dinput8.dll does not match the pinned hash.

  pinned:    $PinnedDllSha256
  downloaded: $dllSha  (tag $($meta.Tag), asset $($meta.AssetName))

This DLL is loaded into the game process, so it is not adopted automatically.
If this is a genuine upstream release, check it, then re-run:

  pixi run update-deps -- -AcceptNewUpstream

and set `$PinnedDllSha256 in scripts/update-deps.ps1 to the downloaded hash in
the same commit.
"@
        }
        Write-Host "  -AcceptNewUpstream: adopting $dllSha (was $PinnedDllSha256)" -ForegroundColor Yellow
        Write-Host "  update `$PinnedDllSha256 in scripts/update-deps.ps1 to match, in this commit." -ForegroundColor Yellow
    }

    # Idempotency: an upstream that has not moved must leave the tree clean. Without
    # this the FetchedAt line rewrites README.md on every run, so `git status` after a
    # no-op refresh shows a timestamp-only diff with no artifact behind it.
    if ((Test-Path -LiteralPath $vendorAsiDll) -and (Test-Path -LiteralPath $vendorAsiLicense) -and (Test-Path -LiteralPath $vendorAsiReadme) -and
        ((Get-FileHash -LiteralPath $vendorAsiDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)) {
        Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
        Write-Host ""
        Write-Host "vendor/ultimate-asi-loader is already up to date." -ForegroundColor Green
        return
    }

    Move-Item -LiteralPath $tempDll -Destination $vendorAsiDll -Force

    if (Test-Path -LiteralPath $tempLicense) {
        Move-Item -LiteralPath $tempLicense -Destination $vendorAsiLicense -Force
    } else {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $vendorAsiLicense -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it to',
        '`<game>/EthanCarter/Binaries/Win64/winmm.dll` as the ASI hook slot (the shipping exe',
        'imports winmm.dll directly; dwmapi.dll is taken by an existing UE4SS install).'
    ) -join "`n"
    # Not Set-Content -Encoding UTF8: on Windows PowerShell 5.1 that writes a
    # UTF-8 BOM, the same defect package-release.ps1 carries a fix for.
    [System.IO.File]::WriteAllText($vendorAsiReadme, $readme + "`n",
        (New-Object System.Text.UTF8Encoding $false))

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
