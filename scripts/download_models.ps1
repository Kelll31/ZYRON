<#
.SYNOPSIS
  Downloads the ONNX models ZYRON plans to use (docs/AI_MODELS.md) from Hugging Face into .\models.

.DESCRIPTION
  Lists the files of each repository through the public Hugging Face API, shows what will be downloaded and how big it
  is, asks for confirmation, then downloads with curl.exe (resumable) and verifies the SHA-256 of every LFS file.
  Finished files are skipped, so the script can simply be run again after an interruption.

  Models (all MIT per their model cards, checked 2026-10-07):
    htdemucs   StemSplitio/htdemucs-onnx   stem separation (htdemucs.onnx, fp32, ~316 MB)
    beat-this  musetric/beat-this-onnx     beats and downbeats (~120 MB)
    skey       musetric/skey-onnx          global key (~0.3 MB)
    chordmini  musetric/chordmini-onnx     chords (~17 MB)

  Weights are never committed (models/ is git-ignored) and never bundled with the application (SPEC section 73).

.PARAMETER Destination
  Target folder. Default: the models folder next to scripts\.

.PARAMETER Yes
  Do not ask for confirmation.

.PARAMETER IncludeDemucsFp16
  Also download the fp16-weights Demucs variant (~166 MB).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File .\scripts\download_models.ps1
#>
param(
  [string]$Destination = (Join-Path $PSScriptRoot '..\models'),
  [switch]$Yes,
  [switch]$IncludeDemucsFp16
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$smallFileLimit = 1MB   # config / readme files next to the weights are always taken

$models = @(
  @{ Repo = 'StemSplitio/htdemucs-onnx'; Dir = 'htdemucs'; Only = @('htdemucs.onnx') },
  @{ Repo = 'musetric/beat-this-onnx';   Dir = 'beat-this' },
  @{ Repo = 'musetric/skey-onnx';        Dir = 'skey' },
  @{ Repo = 'musetric/chordmini-onnx';   Dir = 'chordmini' }
)
if ($IncludeDemucsFp16) {
  $models[0].Only += 'htdemucs_fp16weights.onnx'
}

if (-not (Get-Command curl.exe -ErrorAction SilentlyContinue)) {
  throw 'curl.exe was not found (it ships with Windows 10 and later).'
}

$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $Destination | Out-Null

# 1. Build the file list
$plan = @()
foreach ($model in $models) {
  Write-Host "Listing $($model.Repo) ..."
  $tree = Invoke-RestMethod -Uri "https://huggingface.co/api/models/$($model.Repo)/tree/main?recursive=true"
  foreach ($entry in $tree) {
    if ($entry.type -ne 'file') { continue }
    $take = $true
    if ($model.ContainsKey('Only')) {
      $isWanted = $model.Only -contains $entry.path
      $isSmallSidecar = ($entry.size -lt $smallFileLimit) -and ($entry.path -notlike '*.onnx')
      $take = $isWanted -or $isSmallSidecar
    }
    if (-not $take) { continue }
    $sha = $null
    if ($entry.lfs -and $entry.lfs.oid) { $sha = $entry.lfs.oid }
    $plan += [pscustomobject]@{
      Repo = $model.Repo
      Dir  = $model.Dir
      Path = $entry.path
      Size = [int64]$entry.size
      Sha  = $sha
    }
  }
}

if ($plan.Count -eq 0) { throw 'Nothing to download: the repositories returned no files.' }

# 2. Show the plan
$plan | Sort-Object Repo, Path |
  Format-Table Repo, Path, @{ n = 'MB'; e = { [math]::Round($_.Size / 1MB, 2) }; a = 'right' } -AutoSize
$totalBytes = ($plan | Measure-Object Size -Sum).Sum
Write-Host "Total: $($plan.Count) files, $([math]::Round($totalBytes / 1MB, 1)) MB  ->  $Destination"

$free = (Get-PSDrive -Name ([IO.Path]::GetPathRoot($Destination).Substring(0, 1))).Free
if ($free -lt ($totalBytes * 1.1)) { throw 'Not enough free disk space.' }

if (-not $Yes) {
  $answer = Read-Host 'Download now? (y/N)'
  if ($answer -notmatch '^(y|yes)$') { Write-Host 'Cancelled.'; exit 0 }
}

# 3. Download and verify
$manifest = @()
foreach ($item in $plan) {
  $target = Join-Path (Join-Path $Destination $item.Dir) ($item.Path -replace '/', '\')
  New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null

  $needsDownload = $true
  if (Test-Path $target) {
    $existing = Get-Item $target
    if ($existing.Length -eq $item.Size) {
      if ($item.Sha) {
        $needsDownload = (Get-FileHash $target -Algorithm SHA256).Hash.ToLower() -ne $item.Sha.ToLower()
      } else {
        $needsDownload = $false
      }
    }
  }

  if ($needsDownload) {
    Write-Host "Downloading $($item.Repo)/$($item.Path) ($([math]::Round($item.Size / 1MB, 1)) MB)"
    $url = "https://huggingface.co/$($item.Repo)/resolve/main/$($item.Path)"
    & curl.exe -L --fail --retry 5 --retry-delay 3 -C - -o $target $url
    if ($LASTEXITCODE -ne 0) { throw "Download failed: $url (curl exit code $LASTEXITCODE)" }
  } else {
    Write-Host "Already complete: $($item.Path)"
  }

  $hash = (Get-FileHash $target -Algorithm SHA256).Hash.ToLower()
  if ($item.Sha -and $hash -ne $item.Sha.ToLower()) {
    throw "SHA-256 mismatch for $($item.Path): expected $($item.Sha), got $hash. Delete the file and run again."
  }
  if ((Get-Item $target).Length -ne $item.Size) {
    throw "Size mismatch for $($item.Path)."
  }
  $manifest += [pscustomobject]@{
    repo   = $item.Repo
    file   = (Join-Path $item.Dir ($item.Path -replace '/', '\'))
    size   = $item.Size
    sha256 = $hash
  }
}

# 4. Record what is on disk (the Model Manager reads this later, ROADMAP P5-08)
$record = [pscustomobject]@{
  downloadedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
  files        = $manifest
}
$record | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $Destination 'manifest.json')

Write-Host ''
Write-Host "Done. $($manifest.Count) files verified. Manifest: $(Join-Path $Destination 'manifest.json')"
Write-Host 'Licences: all four models are MIT per their cards; keep the licence files that came with them.'
