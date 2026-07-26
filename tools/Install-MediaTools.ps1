[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Destination,

    [Parameter(Mandatory = $true)]
    [string]$CacheDirectory,

    [string]$LockFile = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.SecurityProtocolType]::Tls12

if ([string]::IsNullOrWhiteSpace($LockFile)) {
    $LockFile = Join-Path $PSScriptRoot "..\resources\media-tools.lock.json"
}

$LockFile = [System.IO.Path]::GetFullPath($LockFile)
$Destination = [System.IO.Path]::GetFullPath($Destination)
$CacheDirectory = [System.IO.Path]::GetFullPath($CacheDirectory)

function Assert-Sha256Text {
    param(
        [Parameter(Mandatory = $true)][string]$Value,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if ($Value -notmatch '^[a-fA-F0-9]{64}$') {
        throw "$Label is not a valid SHA-256 value: $Value"
    }
}

function Get-Sha256Lower {
    param([Parameter(Mandatory = $true)][string]$Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Assert-FileSha256 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Expected,
        [Parameter(Mandatory = $true)][string]$Label
    )

    Assert-Sha256Text -Value $Expected -Label "$Label expected hash"
    $actual = Get-Sha256Lower -Path $Path
    if ($actual -ne $Expected.ToLowerInvariant()) {
        throw "$Label SHA-256 mismatch. Expected $Expected, got $actual."
    }
}

function Save-RemoteFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $parent = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $parent -Force | Out-Null

    $temporary = "$Path.partial"
    Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue

    Write-Host "Downloading $Uri"
    try {
        Invoke-WebRequest `
            -UseBasicParsing `
            -Uri $Uri `
            -OutFile $temporary `
            -MaximumRedirection 10 `
            -Headers @{ "User-Agent" = "SoundBoardFasaFiso-MediaTools" }

        Move-Item -LiteralPath $temporary -Destination $Path -Force
    }
    finally {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
    }
}

function Get-CachedVerifiedFile {
    param(
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$ExpectedSha256,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        try {
            Assert-FileSha256 -Path $Path -Expected $ExpectedSha256 -Label $Label
            Write-Host "Using verified cache: $Path"
            return
        }
        catch {
            Write-Warning "$Label cache entry failed verification and will be replaced."
            Remove-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
        }
    }

    Save-RemoteFile -Uri $Uri -Path $Path
    Assert-FileSha256 -Path $Path -Expected $ExpectedSha256 -Label $Label
}

function Get-ChecksumEntry {
    param(
        [Parameter(Mandatory = $true)][string]$ChecksumPath,
        [Parameter(Mandatory = $true)][string]$FileName
    )

    foreach ($line in Get-Content -LiteralPath $ChecksumPath) {
        $match = [regex]::Match(
            $line.Trim(),
            '^([a-fA-F0-9]{64})\s+\*?(.+)$'
        )

        if (-not $match.Success) {
            continue
        }

        $candidate = [System.IO.Path]::GetFileName($match.Groups[2].Value.Trim())
        if ($candidate -eq $FileName) {
            return $match.Groups[1].Value.ToLowerInvariant()
        }
    }

    throw "No SHA-256 entry for $FileName was found in $ChecksumPath."
}

function Copy-RequiredExecutable {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$DestinationPath
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Required executable is missing from the verified archive: $Source"
    }

    Copy-Item -LiteralPath $Source -Destination $DestinationPath -Force
}

if (-not (Test-Path -LiteralPath $LockFile -PathType Leaf)) {
    throw "Media-tool lock file is missing: $LockFile"
}

$lock = Get-Content -LiteralPath $LockFile -Raw | ConvertFrom-Json

Assert-Sha256Text -Value $lock.ytDlp.sha256 -Label "yt-dlp hash"
Assert-Sha256Text -Value $lock.ytDlp.licenseSha256 -Label "yt-dlp license hash"
Assert-Sha256Text -Value $lock.ytDlp.thirdPartyLicensesSha256 -Label "yt-dlp third-party licenses hash"
Assert-Sha256Text -Value $lock.deno.archiveSha256 -Label "Deno archive hash"
Assert-Sha256Text -Value $lock.deno.licenseSha256 -Label "Deno license hash"
Assert-Sha256Text -Value $lock.ffmpeg.checksumsSha256 -Label "FFmpeg checksum manifest hash"
Assert-Sha256Text -Value $lock.ffmpeg.ffmpegLicenseSha256 -Label "FFmpeg license hash"
Assert-Sha256Text -Value $lock.ffmpeg.buildLicenseSha256 -Label "BtbN FFmpeg-Builds license hash"

New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null
$destinationParent = Split-Path -Parent $Destination
New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null

$staging = Join-Path $destinationParent (".media-tools-staging-" + [guid]::NewGuid().ToString("N"))
$licenses = Join-Path $staging "licenses"
New-Item -ItemType Directory -Path $licenses -Force | Out-Null

$temporaryDirectories = New-Object System.Collections.Generic.List[string]

try {
    $ytDlpCache = Join-Path $CacheDirectory "yt-dlp-$($lock.ytDlp.version).exe"
    Get-CachedVerifiedFile `
        -Uri $lock.ytDlp.url `
        -Path $ytDlpCache `
        -ExpectedSha256 $lock.ytDlp.sha256 `
        -Label "yt-dlp"
    Copy-RequiredExecutable `
        -Source $ytDlpCache `
        -DestinationPath (Join-Path $staging "yt-dlp.exe")

    $denoArchive = Join-Path $CacheDirectory ("deno-$($lock.deno.version)-$($lock.deno.archiveName)")
    Get-CachedVerifiedFile `
        -Uri $lock.deno.archiveUrl `
        -Path $denoArchive `
        -ExpectedSha256 $lock.deno.archiveSha256 `
        -Label "Deno archive"

    $denoExtract = Join-Path $CacheDirectory ("deno-extract-" + [guid]::NewGuid().ToString("N"))
    $temporaryDirectories.Add($denoExtract)
    Expand-Archive -LiteralPath $denoArchive -DestinationPath $denoExtract -Force
    Copy-RequiredExecutable `
        -Source (Join-Path $denoExtract "deno.exe") `
        -DestinationPath (Join-Path $staging "deno.exe")

    $ffmpegChecksums = Join-Path $CacheDirectory "ffmpeg-$($lock.ffmpeg.version)-checksums.sha256"
    Get-CachedVerifiedFile `
        -Uri $lock.ffmpeg.checksumsUrl `
        -Path $ffmpegChecksums `
        -ExpectedSha256 $lock.ffmpeg.checksumsSha256 `
        -Label "BtbN FFmpeg checksum manifest"

    $ffmpegArchiveHash = Get-ChecksumEntry `
        -ChecksumPath $ffmpegChecksums `
        -FileName $lock.ffmpeg.archiveName

    $ffmpegArchive = Join-Path $CacheDirectory ("ffmpeg-$($lock.ffmpeg.version)-$($lock.ffmpeg.archiveName)")
    Get-CachedVerifiedFile `
        -Uri $lock.ffmpeg.archiveUrl `
        -Path $ffmpegArchive `
        -ExpectedSha256 $ffmpegArchiveHash `
        -Label "FFmpeg archive"

    $ffmpegExtract = Join-Path $CacheDirectory ("ffmpeg-extract-" + [guid]::NewGuid().ToString("N"))
    $temporaryDirectories.Add($ffmpegExtract)
    Expand-Archive -LiteralPath $ffmpegArchive -DestinationPath $ffmpegExtract -Force

    $ffmpegExe = Get-ChildItem -LiteralPath $ffmpegExtract -Filter "ffmpeg.exe" -File -Recurse | Select-Object -First 1
    $ffprobeExe = Get-ChildItem -LiteralPath $ffmpegExtract -Filter "ffprobe.exe" -File -Recurse | Select-Object -First 1
    if ($null -eq $ffmpegExe -or $null -eq $ffprobeExe) {
        throw "The verified FFmpeg archive does not contain ffmpeg.exe and ffprobe.exe."
    }

    Copy-RequiredExecutable `
        -Source $ffmpegExe.FullName `
        -DestinationPath (Join-Path $staging "ffmpeg.exe")
    Copy-RequiredExecutable `
        -Source $ffprobeExe.FullName `
        -DestinationPath (Join-Path $staging "ffprobe.exe")

    $licenseDownloads = @(
        @{
            Uri = $lock.ytDlp.licenseUrl
            Hash = $lock.ytDlp.licenseSha256
            CacheName = "yt-dlp-$($lock.ytDlp.version)-LICENSE.txt"
            OutputName = "YT-DLP-LICENSE.txt"
            Label = "yt-dlp license"
        },
        @{
            Uri = $lock.ytDlp.thirdPartyLicensesUrl
            Hash = $lock.ytDlp.thirdPartyLicensesSha256
            CacheName = "yt-dlp-$($lock.ytDlp.version)-THIRD_PARTY_LICENSES.txt"
            OutputName = "YT-DLP-THIRD-PARTY-LICENSES.txt"
            Label = "yt-dlp third-party licenses"
        },
        @{
            Uri = $lock.deno.licenseUrl
            Hash = $lock.deno.licenseSha256
            CacheName = "deno-$($lock.deno.version)-LICENSE.md"
            OutputName = "DENO-LICENSE.md"
            Label = "Deno license"
        },
        @{
            Uri = $lock.ffmpeg.ffmpegLicenseUrl
            Hash = $lock.ffmpeg.ffmpegLicenseSha256
            CacheName = "ffmpeg-$($lock.ffmpeg.version)-COPYING.LGPLv2.1.txt"
            OutputName = "FFMPEG-LGPL-2.1.txt"
            Label = "FFmpeg LGPL license"
        },
        @{
            Uri = $lock.ffmpeg.buildLicenseUrl
            Hash = $lock.ffmpeg.buildLicenseSha256
            CacheName = "btbn-ffmpeg-builds-$($lock.ffmpeg.version)-LICENSE.txt"
            OutputName = "BTBN-FFMPEG-BUILDS-LICENSE.txt"
            Label = "BtbN FFmpeg-Builds license"
        }
    )

    foreach ($licenseDownload in $licenseDownloads) {
        $licenseCache = Join-Path $CacheDirectory $licenseDownload.CacheName
        Get-CachedVerifiedFile `
            -Uri $licenseDownload.Uri `
            -Path $licenseCache `
            -ExpectedSha256 $licenseDownload.Hash `
            -Label $licenseDownload.Label

        Copy-Item `
            -LiteralPath $licenseCache `
            -Destination (Join-Path $licenses $licenseDownload.OutputName) `
            -Force
    }

    Copy-Item `
        -LiteralPath (Join-Path (Split-Path -Parent $LockFile) "MediaToolsREADME.txt") `
        -Destination (Join-Path $staging "README.txt") `
        -Force

    $sourceNotice = @"
SoundBoardFasaFiso bundled media-tool sources
=============================================

Bundle version: $($lock.bundleVersion)

The executable hashes in media-tools.manifest are calculated after extraction.
No tool is installed globally and no PATH or registry value is modified.

yt-dlp $($lock.ytDlp.version)
$($lock.ytDlp.url)

Deno $($lock.deno.version)
$($lock.deno.archiveUrl)

FFmpeg BtbN build $($lock.ffmpeg.version)
$($lock.ffmpeg.archiveUrl)
$($lock.ffmpeg.checksumsUrl)
"@
    Set-Content -LiteralPath (Join-Path $licenses "SOURCES.txt") -Value $sourceNotice -Encoding UTF8

    $manifestLines = @(
        "manifest_version=$($lock.manifestVersion)",
        "bundle_version=$($lock.bundleVersion)",
        "yt-dlp.version=$($lock.ytDlp.version)",
        "yt-dlp.file=yt-dlp.exe",
        "yt-dlp.sha256=$(Get-Sha256Lower (Join-Path $staging 'yt-dlp.exe'))",
        "deno.version=$($lock.deno.version)",
        "deno.file=deno.exe",
        "deno.sha256=$(Get-Sha256Lower (Join-Path $staging 'deno.exe'))",
        "ffmpeg.version=$($lock.ffmpeg.version)",
        "ffmpeg.file=ffmpeg.exe",
        "ffmpeg.sha256=$(Get-Sha256Lower (Join-Path $staging 'ffmpeg.exe'))",
        "ffprobe.version=$($lock.ffmpeg.version)",
        "ffprobe.file=ffprobe.exe",
        "ffprobe.sha256=$(Get-Sha256Lower (Join-Path $staging 'ffprobe.exe'))"
    )

    Set-Content `
        -LiteralPath (Join-Path $staging "media-tools.manifest") `
        -Value $manifestLines `
        -Encoding ASCII

    $backup = "$Destination.previous"
    Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue

    if (Test-Path -LiteralPath $Destination) {
        Move-Item -LiteralPath $Destination -Destination $backup
    }

    try {
        Move-Item -LiteralPath $staging -Destination $Destination
        Remove-Item -LiteralPath $backup -Recurse -Force -ErrorAction SilentlyContinue
    }
    catch {
        Remove-Item -LiteralPath $Destination -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path -LiteralPath $backup) {
            Move-Item -LiteralPath $backup -Destination $Destination
        }
        throw
    }

    Write-Host "Prepared verified media tools in $Destination"
}
finally {
    foreach ($directory in $temporaryDirectories) {
        Remove-Item -LiteralPath $directory -Recurse -Force -ErrorAction SilentlyContinue
    }

    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
