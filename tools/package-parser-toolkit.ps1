<#
.SYNOPSIS
  Packages the parser toolkit the admin offers for download (Parser requests > a request >
  "Parser toolkit"): make-parser.ps1, build-parser.ps1 and the tools they use, so parsers can be
  built from any folder without the source code.

.DESCRIPTION
  Writes frontend-admin\toolkit\pipkite-parser-toolkit.zip. Run it after changing those scripts or
  the app's parser, then rebuild the admin (docker compose up -d --build admin-web).
  No private key goes in: parsers are still signed with the publisher key of the PC that builds them.

.EXAMPLE
  .\tools\package-parser-toolkit.ps1
#>
param([ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $repo "x64\$Configuration"
$stage = Join-Path ([IO.Path]::GetTempPath()) "pipkite-parser-toolkit-$PID"
$out = Join-Path $repo 'frontend-admin\toolkit\pipkite-parser-toolkit.zip'

foreach ($f in 'Pipkite.Engine.exe', 'Pipkite.SignTool.exe', 'parser-template\module.dll', 'trusted_keys') {
    if (-not (Test-Path (Join-Path $bin $f))) { throw "Missing $bin\$f. Build the solution ($Configuration|x64) first." }
}

Remove-Item -Recurse -Force $stage -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$stage\bin\parser-template" | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'make-parser.ps1'), (Join-Path $PSScriptRoot 'build-parser.ps1') $stage
Copy-Item (Join-Path $bin 'Pipkite.Engine.exe'), (Join-Path $bin 'Pipkite.SignTool.exe') "$stage\bin"
Copy-Item (Join-Path $bin 'parser-template\module.dll') "$stage\bin\parser-template"
Copy-Item (Join-Path $bin 'trusted_keys') "$stage\bin" -Recurse
if (Get-ChildItem $stage -Recurse -Include *.key, *.pfx, *.pdb) { throw 'A private key or symbols ended up in the toolkit.' }

@'
Pipkite parser toolkit
======================

1. In the admin, open the parser request and click "Download parser kit".
2. Right-click make-parser.ps1 > "Run with PowerShell" (or in PowerShell: .\make-parser.ps1).
   It takes the newest kit from your Downloads folder, shows what the parser reads from each
   sample message and asks you to confirm. Then it builds, signs and tests the parser and opens
   the "parsers" folder with the .zip selected.
3. Upload that .zip on the same request page (step 3, "Deliver parser"). Only that user's app gets it.

If a trade is misread, answer "n": the script explains what to add to the kit (a word for TP or SL,
a symbol name like GOLD) and opens it; save it and run make-parser.ps1 again.

Signing: parsers are signed with the publisher key in %LOCALAPPDATA%\PipkitePublisher\devkeys.
That key only works on the PC and Windows account that created it, so build parsers there.
If Windows blocks the script, run once: Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
'@ | Set-Content -Encoding UTF8 "$stage\README.txt"

New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
Remove-Item $out -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $out
Remove-Item -Recurse -Force $stage
Write-Host "Parser toolkit: $out ($([int]((Get-Item $out).Length / 1KB)) KB)"
