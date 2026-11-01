<#
.SYNOPSIS
  Builds a signed custom parser from a parser kit downloaded from the admin panel.

.DESCRIPTION
  1. In the admin, open the parser request and click "Download parser kit".
  2. Edit the kit: the "template" rules (words, labels, symbol names) and the expected result of
     each sample in "fixtures" (expect "signal" with side/symbol/entryLow/entryHigh/stopLoss/
     takeProfits, or "ignore" for chatter, or "review" for messages it must not trade).
  3. Run this script. It writes the module (the generic template parser + a manifest holding the
     rules and the samples), signs it with this PC's publisher key, then checks the signature and
     runs every sample through the parser the way the app does.
  4. Upload the .zip it prints on the same request page in the admin ("Deliver parser").

  To change a delivered parser, raise "version" in the kit and run it again.

.PARAMETER Kit
  The kit JSON file, e.g. parser-kit-gold_snipers_vip.json

.PARAMETER Configuration
  Which build of the app to use for signing and testing (Release by default).

.PARAMETER Preview
  Only run the samples and return the validation result (with what the parser read from each
  sample), without printing, failing or zipping. Used by tools\make-parser.ps1.

.EXAMPLE
  .\tools\build-parser.ps1 -Kit $HOME\Downloads\parser-kit-gold_snipers_vip.json
#>
param(
    [Parameter(Mandatory = $true)][string]$Kit,
    [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
    [switch]$Preview
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
# Runs from the repository (tools\ next to x64\Release) or from the parser toolkit downloaded from the
# admin (bin\ next to this script, made by tools\package-parser-toolkit.ps1).
$toolkit = Test-Path (Join-Path $PSScriptRoot 'bin\Pipkite.SignTool.exe')
$bin = if ($toolkit) { Join-Path $PSScriptRoot 'bin' } else { Join-Path $repo "x64\$Configuration" }
$signTool = Join-Path $bin 'Pipkite.SignTool.exe'
$engine = Join-Path $bin 'Pipkite.Engine.exe'
$module = Join-Path $bin 'parser-template\module.dll'
$key = Join-Path $env:LOCALAPPDATA 'PipkitePublisher\devkeys\publisher.key'
$trusted = Join-Path $bin 'trusted_keys'

if (-not (Test-Path $key)) {
    throw "No publisher key at $key. Parsers must be signed with the key the app trusts, and that key only works on the PC and Windows account that created it: build parsers there."
}
foreach ($f in $signTool, $engine, $trusted) {
    if (-not (Test-Path $f)) { throw "Missing $f. Build the solution ($Configuration|x64) first, or use the parser toolkit from the admin." }
}
if (-not (Test-Path $module)) {
    $msbuild = 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe'
    if (-not (Test-Path $msbuild)) { throw "Missing $module. Build Pipkite.Plugin.Template ($Configuration|x64) first." }
    & $msbuild (Join-Path $repo 'Pipkite.Plugin.Template\Pipkite.Plugin.Template.vcxproj') "-p:Configuration=$Configuration" '-p:Platform=x64' "-p:SolutionDir=$repo\" -m -nologo -v:minimal
    if ($LASTEXITCODE -ne 0) { throw 'Building the template parser failed.' }
}

# --- read and check the kit ------------------------------------------------------------------
$k = Get-Content -Raw -Encoding UTF8 $Kit | ConvertFrom-Json
if ($k.kit -ne 'pipkite-parser-kit-v1') { throw 'This is not a Pipkite parser kit (download it from the parser request in the admin).' }
if ($k.id -notmatch '^[a-z0-9_]{3,64}$') { throw "id '$($k.id)' must be 3-64 characters: a-z, 0-9 and _." }
if ($k.version -notmatch '^\d{1,4}\.\d{1,4}\.\d{1,4}$') { throw "version '$($k.version)' must look like 1.0.0." }
$fixtures = @($k.fixtures)
if (-not ($fixtures | Where-Object { $_.expect -eq 'signal' })) { throw 'Mark at least one sample with "expect": "signal".' }
foreach ($fx in $fixtures) {
    if ($fx.expect -notin 'signal', 'ignore', 'review') { throw "Sample $($fx.id): expect must be signal, ignore or review." }
}
$template = $k.template
if ($null -eq $template) { $template = [pscustomobject]@{} }
# the module takes its id and version from the manifest
$template.PSObject.Properties.Remove('id')
$template.PSObject.Properties.Remove('version')

# --- write, sign and check the module ----------------------------------------------------------
$name = "$($k.id)-$($k.version)"
$outRoot = if ($toolkit) { Join-Path $PSScriptRoot 'parsers' } else { Join-Path $repo 'dist\parsers' }
$out = Join-Path $outRoot $name
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item $module (Join-Path $out 'module.dll')

$manifest = [ordered]@{
    format       = 'pipkite-module-v1'
    moduleType   = 'parser'
    id           = $k.id
    version      = $k.version
    abiVersion   = 1
    architecture = 'x64'
    entry        = 'module.dll'
    publisher    = 'Pipkite'
    description  = "Custom parser for $($k.channelTitle) (request $($k.requestId))"
    template     = $template
    fixtures     = $fixtures
    sha256       = ''
    signature    = ''
}
$manifestPath = Join-Path $out 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding $false))

$quiet = if ($Preview) { 'Out-Null' } else { 'Out-Host' }
& $signTool sign-module $manifestPath $key | & $quiet
if ($LASTEXITCODE -ne 0) { throw 'Signing failed.' }
& $signTool verify $manifestPath $trusted | & $quiet
if ($LASTEXITCODE -ne 0) { throw 'The signed module does not verify against trusted_keys.' }

# the app's own check: load the module in a helper process and run every sample through it
$result = Join-Path $out 'validation.json'
$p = Start-Process -FilePath $engine -ArgumentList '--validate-module', "`"$manifestPath`"", '--out', "`"$result`"" -Wait -PassThru -WindowStyle Hidden
if (-not (Test-Path $result)) { throw "Validation did not run (exit $($p.ExitCode))." }
$v = Get-Content -Raw -Encoding UTF8 $result | ConvertFrom-Json
Remove-Item $result
if ($Preview) { return $v }
if ($v.suite) {
    $v.suite.results | ForEach-Object {
        $mark = if ($_.passed) { 'ok  ' } else { 'FAIL' }
        Write-Host ("{0} {1,-28} {2,-7} {3}" -f $mark, $_.id, $_.actual, ($_.differences -join '; '))
    }
}
if (-not $v.ok) {
    throw "The parser failed validation: $($v.error). Fix the template or the expected values in the kit and run again. (Samples named neg_* are built-in messages every parser must refuse.)"
}

$zip = Join-Path $outRoot "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $manifestPath, (Join-Path $out 'module.dll') -DestinationPath $zip
Write-Host ''
Write-Host "Parser ready: $zip"
Write-Host 'Upload it on the parser request page in the admin (Deliver parser).'
