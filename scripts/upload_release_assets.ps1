param(
    [string]$Version = "",
    [switch]$PauseOnExit
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = '0.1.0'
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoDir = [IO.Path]::GetFullPath((Join-Path $scriptDir '..'))
$distDir = Join-Path $repoDir 'dist'
$logFile = Join-Path $distDir 'upload_release_assets.log'

$installerName = "glassNote-$Version-win64-setup.exe"
$installerPath = Join-Path $distDir $installerName
$manifestPath = Join-Path $distDir 'update-manifest.json'
$versionedManifestPath = Join-Path $distDir ("update-manifest-{0}.json" -f $Version)

$releaseRepo = $env:GLASSNOTE_RELEASE_REPO
if ([string]::IsNullOrWhiteSpace($releaseRepo)) {
    $releaseRepo = 'kakuyo1/glassNote'
}

$releaseTag = $env:GLASSNOTE_RELEASE_TAG
if ([string]::IsNullOrWhiteSpace($releaseTag)) {
    $releaseTag = "V$Version"
}

if (-not (Test-Path -LiteralPath $distDir)) {
    New-Item -ItemType Directory -Path $distDir | Out-Null
}

@(
    '[glassNote] Release asset upload started'
    "[glassNote] Repo: $repoDir"
    "[glassNote] Version: $Version"
) | Set-Content -LiteralPath $logFile -Encoding UTF8

function Write-Log {
    param([string]$Message)
    if ($null -eq $Message) {
        $Message = ''
    }
    Write-Host $Message
    Add-Content -LiteralPath $logFile -Value $Message -Encoding UTF8
}

function Fail-And-Exit {
    param([string]$Message)
    if (-not [string]::IsNullOrWhiteSpace($Message)) {
        Write-Log $Message
    }
    Write-Log ''
    Write-Log '[FAILED] Upload did not complete.'
    Write-Log "         Check log file: $logFile"
    if ($PauseOnExit) {
        Read-Host 'Press Enter to exit'
    }
    exit 1
}

Write-Log "[glassNote] Uploading release assets for $Version"
Write-Log "[INFO] GitHub repo: $releaseRepo"
Write-Log "[INFO] Release tag: $releaseTag"

$gh = Get-Command gh -ErrorAction SilentlyContinue
if ($null -eq $gh) {
    Fail-And-Exit '[ERROR] gh CLI is not available in PATH.'
}

Write-Log "[INFO] Using gh: $($gh.Source)"

$authOutput = & $gh.Source auth status -h github.com 2>&1
$authOutput | Add-Content -LiteralPath $logFile -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    Fail-And-Exit '[ERROR] gh auth status failed. Please run: gh auth login'
}

if (-not (Test-Path -LiteralPath $installerPath)) {
    Fail-And-Exit "[ERROR] Installer not found: $installerPath"
}

if (-not (Test-Path -LiteralPath $manifestPath)) {
    Fail-And-Exit "[ERROR] Manifest not found: $manifestPath"
}

if (Test-Path -LiteralPath $versionedManifestPath) {
    Copy-Item -LiteralPath $versionedManifestPath -Destination $manifestPath -Force
    Write-Log "[INFO] Synced versioned manifest: $versionedManifestPath"
}

try {
    $manifestObject = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
} catch {
    Fail-And-Exit "[ERROR] Failed to parse manifest JSON: $manifestPath"
}

if ($manifestObject.version -ne $Version) {
    Fail-And-Exit ("[ERROR] Manifest version does not match {0}: {1}" -f $Version, $manifestPath)
}

$viewOutput = & $gh.Source release view $releaseTag -R $releaseRepo 2>&1
$viewOutput | Add-Content -LiteralPath $logFile -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    Write-Log "[ERROR] Release tag not found: $releaseTag"
    Fail-And-Exit '        Create the GitHub Release first, then retry.'
}

Write-Log '[1/1] Upload installer and update-manifest.json'
$uploadOutput = & $gh.Source release upload $releaseTag $installerPath $manifestPath --clobber -R $releaseRepo 2>&1
$uploadOutput | Add-Content -LiteralPath $logFile -Encoding UTF8
if ($LASTEXITCODE -ne 0) {
    Fail-And-Exit '[ERROR] gh release upload failed.'
}

Write-Log ''
Write-Log '[DONE] Upload succeeded.'
Write-Log "       Uploaded: $installerPath"
Write-Log "       Uploaded: $manifestPath"
Write-Log "       Log file: $logFile"

if ($PauseOnExit) {
    Read-Host 'Press Enter to exit'
}

exit 0
