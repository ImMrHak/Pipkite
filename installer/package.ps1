<#
.SYNOPSIS
  Builds the Pipkite 1.0.0 distributable: a portable per-user zip and (when Inno Setup 6/7 is installed)
  a Windows installer. Symbols are archived separately and never shipped.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File installer\package.ps1 -Build
#>
param(
    [string]$Configuration = "Release",
    [switch]$Build,
    [string]$Version = "1.0.0"
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $repo "x64\$Configuration"
$dist = Join-Path $repo "dist"
$stage = Join-Path $dist "Pipkite-$Version"

function Find-VS {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $path = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $path) { throw "Visual Studio with MSBuild not found" }
    return $path
}

$vs = Find-VS
if ($Build) {
    & "$vs\MSBuild\Current\Bin\amd64\MSBuild.exe" (Join-Path $repo "Pipkite.sln") -p:Configuration=$Configuration -p:Platform=x64 -m -nologo -v:minimal
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}
if (-not (Test-Path "$bin\Pipkite.exe")) { throw "Build output not found in $bin (run with -Build)" }

# Authenticode-sign the build output (free self-signed cert) so Windows Security does not block it, then the
# already-signed binaries are copied into the package below. See tools\sign-release.ps1.
& (Join-Path $PSScriptRoot "..\tools\sign-release.ps1") -Dir $bin
if ($LASTEXITCODE -ne 0) { throw "Signing failed" }

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null

# Application binaries (static native runtime; the C++/CLI UI needs VCRUNTIME140 app-locally + .NET 4.8).
$files = "Pipkite.exe", "Pipkite.exe.config", "Pipkite.Engine.exe", "Pipkite.Launcher.exe", "MT5BridgeHost.dll"
foreach ($f in $files) { Copy-Item "$bin\$f" $stage }
# Ship EMPTY: bundle no parser modules. Every parser is delivered per-license by the backend updater, so a
# fresh install starts with no data and an empty plugins folder. (bridge = MT5 bridge, trusted_keys = update
# verification key, mql5 = the EA; all required infrastructure, not customer data.)
foreach ($d in "bridge", "trusted_keys", "mql5") { Copy-Item "$bin\$d" "$stage\$d" -Recurse }
New-Item -ItemType Directory -Force "$stage\plugins" | Out-Null
if (-not (Test-Path "$bin\mql5\PipkiteEA.ex5")) { throw "Compiled EA missing (mql5\PipkiteEA\PipkiteEA.ex5); run mql5\compile-ea.ps1" }
# Telegram: TDLib and its OpenSSL/zlib runtime (built by tools\build-tdlib.ps1, staged by the build).
$tdlibDlls = "tdjson.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll", "z.dll"
foreach ($f in $tdlibDlls) {
    if (-not (Test-Path "$bin\$f")) { throw "$f missing from $bin; run tools\build-tdlib.ps1 and rebuild" }
    Copy-Item "$bin\$f" $stage
}
# Module folders contain linker by-products next to the DLLs; symbols go to the private archive instead.
Get-ChildItem "$stage\bridge", "$stage\plugins" -Recurse -Include *.pdb, *.lib, *.exp | Remove-Item -Force

$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory | Sort-Object FullName -Descending | Select-Object -First 1
foreach ($dll in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll") { Copy-Item (Join-Path $crt.FullName $dll) $stage }
$redist = Get-ChildItem "$vs\VC\Redist\MSVC\*\vc_redist.x64.exe" | Sort-Object FullName -Descending | Select-Object -First 1
if ($redist) { New-Item -ItemType Directory -Force "$stage\redist" | Out-Null; Copy-Item $redist.FullName "$stage\redist\" }

# Docs aren't in git, so only ship them when they exist locally.
foreach ($f in "README.md", "CHANGELOG.md") {
    if (Test-Path (Join-Path $repo $f)) { Copy-Item (Join-Path $repo $f) $stage }
}
if (Test-Path (Join-Path $repo "docs")) { Copy-Item (Join-Path $repo "docs") "$stage\docs" -Recurse }
else { Write-Warning "docs\ not found, packaging without the user guide" }
New-Item -ItemType Directory -Force "$stage\licenses" | Out-Null
Copy-Item (Join-Path $repo "third_party\nlohmann\LICENSE.MIT") "$stage\licenses\nlohmann-json-LICENSE.MIT.txt"
Copy-Item (Join-Path $repo "third_party\THIRD_PARTY_NOTICES.md") "$stage\licenses\"
Copy-Item "$bin\licenses\*" "$stage\licenses\" -Force   # TDLib (Boost), OpenSSL (Apache-2.0), zlib

# Public code-signing certificate, so the installer can trust our signature on each user's machine.
& (Join-Path $PSScriptRoot "..\tools\sign-release.ps1") -Dir $bin -ExportCer (Join-Path $stage "Pipkite-publisher.cer")
if (-not (Test-Path "$stage\Pipkite-publisher.cer")) { throw "Publisher certificate was not exported" }

# Release hygiene checks: no source, no private keys, no build intermediates, no symbols.
$forbidden = Get-ChildItem $stage -Recurse -Include *.key, *.pfx, *.pdb, *.lib, *.exp, *.obj, *.cpp, *.h, *.ilk, *.metagen
if ($forbidden) { throw "Forbidden files in package: $($forbidden.FullName -join ', ')" }
if (Test-Path "$stage\trusted_keys\dev-publisher.pub") {
    Write-Warning "Package contains the LOCAL DEVELOPMENT publisher key. Production releases must replace trusted_keys with the real publisher key and re-sign modules on the protected build machine."
}

# Private symbols archive (kept by the publisher, never shipped).
$symbols = Join-Path $dist "Pipkite-$Version-symbols-PRIVATE.zip"
Remove-Item $symbols -ErrorAction SilentlyContinue
Compress-Archive -Path (Get-ChildItem $bin -Recurse -Filter *.pdb).FullName -DestinationPath $symbols

$zip = Join-Path $dist "Pipkite-$Version-win-x64.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
Set-Content -Path "$zip.sha256" -Value "$hash  $(Split-Path $zip -Leaf)"
Write-Host "Portable package: $zip"
Write-Host "SHA-256: $hash"

# Inno Setup 6 or 7 (the .iss is compatible with both).
$isccCandidates = @()
foreach ($base in "${env:ProgramFiles(x86)}", "$env:ProgramFiles", "$env:LOCALAPPDATA\Programs") {
    foreach ($v in "Inno Setup 7", "Inno Setup 6") { $isccCandidates += (Join-Path $base "$v\ISCC.exe") }
}
$iscc = $isccCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
    & $iscc "/DAppVersion=$Version" "/DStageDir=$stage" "/O$dist" (Join-Path $PSScriptRoot "Pipkite.iss")
    if ($LASTEXITCODE -ne 0) { throw "Installer compilation failed" }
} else {
    Write-Host "Inno Setup not found: skipped Pipkite-$Version-setup.exe (install Inno Setup and re-run to produce it)."
}
