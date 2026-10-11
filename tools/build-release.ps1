<#
.SYNOPSIS
  Release build: clean-rebuild x64 Release, Authenticode-sign the binaries, run the test suite, and package
  (portable zip + Inno Setup installer).

.DESCRIPTION
  Release hardening / compatibility steps:
   - Native Release binaries are built with High-Entropy ASLR, DEP and Control Flow Guard (Pipkite.Native.props).
   - Binaries are Authenticode-signed (tools\sign-release.ps1) so Windows Security (SmartScreen / Smart App
     Control) does not block them. By default this uses a free, persistent self-signed certificate; the
     installer trusts its public cert per-user. Signing happens BEFORE the tests so the test suite can launch
     the freshly built helper processes on Smart-App-Control PCs. For unconditional trust on any machine,
     replace the self-signed cert with an EV certificate.
   - package.ps1 ships only required binaries, archives PDBs privately, and refuses source/keys/symbols.
#>
param(
    [string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot

function Run([string]$exe, [string[]]$argv) {
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe exited $LASTEXITCODE" }
}

$stamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
$sln = Join-Path $repo "Pipkite.sln"
$bin = Join-Path $repo "x64\$Configuration"

# 1) Clean rebuild (hardening flags from Pipkite.Native.props).
Run $msbuild @($sln, "/t:Rebuild", "/p:Configuration=$Configuration", "/p:Platform=x64", "-m", "-v:m", "-nologo")
# 2) Sign the build output BEFORE tests, so test helper processes aren't blocked by Smart App Control.
& (Join-Path $PSScriptRoot "sign-release.ps1") -Dir $bin
if ($LASTEXITCODE -ne 0) { throw "Signing failed" }
# 3) Tests must pass on the signed build.
Run (Join-Path $bin "Pipkite.Tests.exe") @()
# 4) Package (portable zip + installer). Called directly so the named parameter binds.
& (Join-Path $PSScriptRoot "..\installer\package.ps1") -Configuration $Configuration
Add-Content -Encoding utf8 (Join-Path $repo "dist\Pipkite-release-manifest.txt") "build $stamp  signed(self)  config $Configuration"
Write-Host "Release build complete."
