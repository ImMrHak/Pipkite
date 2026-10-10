<#
.SYNOPSIS
  Signs Pipkite module manifests in a build output folder with a LOCAL DEVELOPMENT publisher key.

.DESCRIPTION
  - Creates the dev key once under %LOCALAPPDATA%\Pipkite\devkeys (never inside the repository).
  - Signs bridge\impl\*\manifest.json and plugins\*\*\manifest.json (hash + ECDSA P-256 signature).
  - Installs the dev public key as trusted_keys\dev-publisher.pub in the output folder.
  - Copies the MQL5 EA (source + compiled .ex5) and TDLib (tdjson.dll + dependencies) next to the binaries.
  Production releases are signed with the publisher's private key on a protected build machine instead.

.PARAMETER OutDir
  Build output folder, e.g. x64\Debug\
#>
param([Parameter(Mandatory = $true)][string]$OutDir)

$ErrorActionPreference = 'Stop'
$OutDir = (Resolve-Path $OutDir).Path
$tool = Join-Path $OutDir 'Pipkite.SignTool.exe'
if (-not (Test-Path $tool)) { Write-Host "SignTool not built yet; skipping module signing."; exit 0 }

$keyDir = Join-Path $env:LOCALAPPDATA 'Pipkite\devkeys'
if (-not (Test-Path (Join-Path $keyDir 'publisher.key'))) { & $tool keygen $keyDir | Out-Host }
$key = Join-Path $keyDir 'publisher.key'

$trusted = Join-Path $OutDir 'trusted_keys'
New-Item -ItemType Directory -Force $trusted | Out-Null
Copy-Item (Join-Path $keyDir 'publisher.pub') (Join-Path $trusted 'dev-publisher.pub') -Force
# Ship any additional publisher public keys committed in the repo (e.g. pipkite-backend.pub, which the
# backend uses to sign update release manifests). Public keys only; private keys never live in the repo.
$repoRoot = Split-Path -Parent $PSScriptRoot
$repoTrusted = Join-Path $repoRoot 'trusted_keys'
if (Test-Path $repoTrusted) { Copy-Item (Join-Path $repoTrusted '*.pub') $trusted -Force -ErrorAction SilentlyContinue }

$manifests = @()
$manifests += Get-ChildItem -Path (Join-Path $OutDir 'bridge\impl') -Filter manifest.json -Recurse -ErrorAction SilentlyContinue
$manifests += Get-ChildItem -Path (Join-Path $OutDir 'plugins') -Filter manifest.json -Recurse -ErrorAction SilentlyContinue
foreach ($m in $manifests) {
    & $tool sign-module $m.FullName $key | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $($m.FullName)" }
}

$repo = Split-Path -Parent $PSScriptRoot
$mqlOut = Join-Path $OutDir 'mql5'
New-Item -ItemType Directory -Force $mqlOut | Out-Null
Copy-Item (Join-Path $repo 'mql5\PipkiteEA\PipkiteEA.mq5') $mqlOut -Force -ErrorAction SilentlyContinue
Copy-Item (Join-Path $repo 'mql5\PipkiteEA\PipkiteEA.ex5') $mqlOut -Force -ErrorAction SilentlyContinue

# TDLib (tdjson.dll + OpenSSL/zlib) ships next to Pipkite.Engine.exe; build it with tools\build-tdlib.ps1.
$tdlib = Join-Path $repo 'third_party\tdlib'
if (Test-Path (Join-Path $tdlib 'bin\tdjson.dll')) {
    Copy-Item (Join-Path $tdlib 'bin\*.dll') $OutDir -Force
    # tdjson.dll needs the VC++ runtime; copy it next to the binaries so TDLib loads in dev as it does when installed.
    $vcDir = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
    if ($vcDir) { foreach ($d in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') { Copy-Item (Join-Path $vcDir.FullName $d) $OutDir -Force -ErrorAction SilentlyContinue } }
    $lic = Join-Path $OutDir 'licenses'
    New-Item -ItemType Directory -Force $lic | Out-Null
    Copy-Item (Join-Path $tdlib 'licenses\*') $lic -Force
    Copy-Item (Join-Path $tdlib 'VERSION.txt') (Join-Path $lic 'TDLib-VERSION.txt') -Force
} else {
    Write-Host "warning: third_party\tdlib\bin\tdjson.dll not found; run tools\build-tdlib.ps1 to ship Telegram support."
}
Write-Host "Dev module signing complete ($($manifests.Count) manifest(s))."
