[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$SourceRoot = "C:\Dev\rnnoise-v0.2-spike",

    [Parameter(Mandatory = $false)]
    [string]$DestinationRoot = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($DestinationRoot)) {
    $DestinationRoot = Join-Path $PSScriptRoot "..\third_party\rnnoise"
}
$DestinationRoot = [System.IO.Path]::GetFullPath($DestinationRoot)

$ExpectedCommit = "904a876dce1f9ab8860c0a5000ed151f9f6eef58"
$ExpectedModelArchiveHash =
    "4AC81C5C0884EC4BD5907026AAAE16209B7B76CD9D7F71AF582094A2F98F4B43"
$ModelArchiveName = "rnnoise_data-0b50c45.tar.gz"

if (-not (Test-Path $SourceRoot -PathType Container)) {
    throw "RNNoise source folder was not found: $SourceRoot"
}

$actualCommit = (& git -C $SourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $ExpectedCommit) {
    throw "RNNoise source must be v0.2 commit $ExpectedCommit; found $actualCommit"
}

$modelArchive = Join-Path $SourceRoot $ModelArchiveName
if (-not (Test-Path $modelArchive -PathType Leaf)) {
    throw "RNNoise model archive was not found: $modelArchive"
}

$actualModelHash = (Get-FileHash $modelArchive -Algorithm SHA256).Hash
if ($actualModelHash -ne $ExpectedModelArchiveHash) {
    throw "RNNoise model archive SHA-256 mismatch: $actualModelHash"
}

$requiredFiles = @(
    "COPYING",
    "include\rnnoise.h",
    "src\_kiss_fft_guts.h",
    "src\arch.h",
    "src\celt_lpc.c",
    "src\celt_lpc.h",
    "src\common.h",
    "src\cpu_support.h",
    "src\denoise.c",
    "src\denoise.h",
    "src\kiss_fft.c",
    "src\kiss_fft.h",
    "src\nnet.c",
    "src\nnet.h",
    "src\nnet_arch.h",
    "src\nnet_default.c",
    "src\opus_types.h",
    "src\parse_lpcnet_weights.c",
    "src\pitch.c",
    "src\pitch.h",
    "src\rnn.c",
    "src\rnn.h",
    "src\rnnoise_data.h",
    "src\rnnoise_data_little.c",
    "src\rnnoise_data_little.h",
    "src\rnnoise_tables.c",
    "src\vec.h",
    "src\x86\x86_arch_macros.h"
)

foreach ($relativePath in $requiredFiles) {
    $sourcePath = Join-Path $SourceRoot $relativePath
    if (-not (Test-Path $sourcePath -PathType Leaf)) {
        throw "Required RNNoise file is missing: $sourcePath"
    }
}

New-Item $DestinationRoot -ItemType Directory -Force | Out-Null
New-Item (Join-Path $DestinationRoot "include") -ItemType Directory -Force |
    Out-Null
New-Item (Join-Path $DestinationRoot "src") -ItemType Directory -Force |
    Out-Null

foreach ($relativePath in $requiredFiles) {
    $sourcePath = Join-Path $SourceRoot $relativePath
    $destinationPath = Join-Path $DestinationRoot $relativePath
    $destinationDirectory = Split-Path $destinationPath -Parent
    New-Item $destinationDirectory -ItemType Directory -Force | Out-Null
    Copy-Item $sourcePath $destinationPath -Force
}

@'
#ifndef OS_SUPPORT_H
#define OS_SUPPORT_H

#include <stddef.h>
#include <string.h>

#ifndef OPUS_CLEAR
#define OPUS_CLEAR(destination, count) \
    memset((destination), 0, (size_t)(count) * sizeof(*(destination)))
#endif

#endif /* OS_SUPPORT_H */
'@ | Set-Content (
    Join-Path $DestinationRoot "src\os_support.h"
) -Encoding ascii

Write-Host "Imported RNNoise v0.2 little model into: $DestinationRoot"
Write-Host "Source commit: $actualCommit"
Write-Host "Model archive SHA-256: $actualModelHash"
