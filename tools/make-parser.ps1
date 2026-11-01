<#
.SYNOPSIS
  Makes a custom parser from a parser kit, step by step, and leaves a .zip ready to upload.

.DESCRIPTION
  1. In the admin, open the parser request and click "Download parser kit".
  2. Run this script. It takes the newest kit in Downloads (or -Kit), runs every sample through the
     parser and shows what it reads: side, symbol, entry, SL and TPs. Answer Y when that is right.
  3. Your answers become the expected results in the kit, so a wrong reading fails the build.
     The script then builds, signs and tests the parser (tools\build-parser.ps1) and opens the folder
     with the .zip selected.
  4. Upload that .zip on the same request page ("Deliver parser").

  If the parser misreads a trade, the script says what to change in the kit's "template" (labels,
  keywords, symbol names), opens the kit, and you run it again.

.PARAMETER Kit
  The kit JSON file. Default: the newest parser-kit-*.json in your Downloads folder.

.PARAMETER Yes
  Accept every reading without asking (only when you already know they are right).

.EXAMPLE
  .\tools\make-parser.ps1
#>
param(
    [string]$Kit,
    [switch]$Yes
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
$repo = Split-Path -Parent $PSScriptRoot

function Save-Kit($k) {
    [IO.File]::WriteAllText($Kit, ($k | ConvertTo-Json -Depth 20), (New-Object Text.UTF8Encoding $false))
}

function Read-Message {
    Write-Host '  Paste the message, then type END on its own line:' -ForegroundColor DarkGray
    $lines = @()
    while ($true) {
        $line = Read-Host
        if ($line.Trim() -eq 'END') { break }
        $lines += $line
    }
    return ($lines -join "`n").Trim()
}

function Format-Price($x) { ([double]$x).ToString([Globalization.CultureInfo]::InvariantCulture) }  # 3936, not 3936.0

function Format-Reading($r) {
    if ($r.actual -eq 'ignore') { return 'IGNORED (chatter, not a trade)' }
    if ($r.actual -ne 'signal') { return "NEEDS REVIEW, not a clean trade ($($r.status))" }
    $s = $r.read
    $entry = if ($null -eq $s.entryLow) { 'market' } elseif ($s.entryLow -eq $s.entryHigh) { Format-Price $s.entryLow } else { "$(Format-Price $s.entryLow) - $(Format-Price $s.entryHigh)" }
    $sl = if ($null -eq $s.stopLoss) { 'none' } else { Format-Price $s.stopLoss }
    return "$($s.side) $($s.symbol)   entry $entry   SL $sl   TP $((@($s.takeProfits) | ForEach-Object { Format-Price $_ }) -join ', ')"
}

# --- the kit -----------------------------------------------------------------------------------
if (-not $Kit) {
    $latest = Get-ChildItem (Join-Path $HOME 'Downloads') -Filter 'parser-kit-*.json' -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $latest) { throw 'No parser-kit-*.json in Downloads. Download it from the parser request in the admin, or pass -Kit <file>.' }
    $Kit = $latest.FullName
}
$Kit = (Resolve-Path $Kit).Path
$k = Get-Content -Raw -Encoding UTF8 $Kit | ConvertFrom-Json
if ($k.kit -ne 'pipkite-parser-kit-v1') { throw "$Kit is not a Pipkite parser kit." }
Write-Host ''
Write-Host "Parser for: $($k.channelTitle)   (id $($k.id), version $($k.version))" -ForegroundColor Cyan
Write-Host "Kit: $Kit"

$fixtures = [System.Collections.ArrayList]@(@($k.fixtures) | Where-Object { $_ })
if ($fixtures.Count -eq 0) {
    if ($Yes) { throw 'The kit has no sample messages; run without -Yes to paste one.' }
    Write-Host ''
    Write-Host 'The request has no sample message. Paste one trade message from the channel.'
    $fixtures.Add([pscustomobject]@{ id = 'sample_1'; text = (Read-Message); expect = 'signal' }) | Out-Null
}
if (-not $Yes) {
    while ((Read-Host 'Add another sample message (a different layout, a SELL, chatter)? [y/N]') -match '^[yY]') {
        $fixtures.Add([pscustomobject]@{ id = "sample_$($fixtures.Count + 1)"; text = (Read-Message); expect = 'signal' }) | Out-Null
    }
}
$k.fixtures = @($fixtures)
Save-Kit $k

# --- what the parser reads ---------------------------------------------------------------------
Write-Host ''
Write-Host 'Reading the samples with the parser...'
$v = & (Join-Path $PSScriptRoot 'build-parser.ps1') -Kit $Kit -Preview
$byId = @{}
foreach ($r in @($v.suite.results)) { $byId[$r.id] = $r }

$misread = @()
foreach ($f in $k.fixtures) {
    $r = $byId[$f.id]
    Write-Host ''
    Write-Host "--- $($f.id) ---" -ForegroundColor Cyan
    ($f.text -split "`r?`n") | ForEach-Object { Write-Host "  | $_" -ForegroundColor DarkGray }
    Write-Host "  Pipkite reads: $(Format-Reading $r)" -ForegroundColor Yellow
    $answer = if ($Yes) { 'y' } else { Read-Host '  Is that right? [Y/n]' }
    if ($answer -eq '' -or $answer -match '^[yY]') {
        # the reading becomes the expected result, so a later change that misreads it fails the build
        foreach ($p in 'side', 'symbol', 'entryLow', 'entryHigh', 'stopLoss', 'takeProfits') { $f.PSObject.Properties.Remove($p) }
        $f.expect = $r.actual
        if ($r.actual -eq 'signal') {
            foreach ($p in $r.read.PSObject.Properties) { $f | Add-Member -NotePropertyName $p.Name -NotePropertyValue $p.Value -Force }
        }
        continue
    }
    $want = Read-Host '  What should it be? [s]ignal (a trade to copy) / [i]gnore (chatter) / [r]eview (update like move SL)'
    $f.expect = switch -regex ($want) { '^[iI]' { 'ignore' } '^[rR]' { 'review' } default { 'signal' } }
    if ($f.expect -eq 'signal') { $misread += $f.id }
}
Save-Kit $k

if ($misread.Count -gt 0) {
    Write-Host ''
    Write-Host "The parser misreads: $($misread -join ', '). Teach it in the kit's ""template"", then run this script again:" -ForegroundColor Red
    Write-Host '  - a word for TP/SL/entry it does not know:   "labels": { "tp": ["TP", "TARGET", "TGT"], "sl": ["SL", "STOP"] }'
    Write-Host '  - a word for buy/sell it does not know:      "keywords": { "buy": ["BUY", "LONG", "ACHAT"], "sell": ["SELL", "SHORT"] }'
    Write-Host '  - a symbol name like GOLD:                   "symbolAliases": { "GOLD": "XAUUSD", "XAUUSD": "XAUUSD" }  (list every symbol)'
    Write-Host '  - numbers written 3940,5:                    "numberFormat": "comma_decimal"'
    Write-Host '  The "help" list at the top of the kit has the other options.'
    Start-Process notepad.exe $Kit
    exit 1
}

# --- build, sign, test and zip -----------------------------------------------------------------
Write-Host ''
& (Join-Path $PSScriptRoot 'build-parser.ps1') -Kit $Kit
$outRoot = if (Test-Path (Join-Path $PSScriptRoot 'bin\Pipkite.SignTool.exe')) { Join-Path $PSScriptRoot 'parsers' } else { Join-Path $repo 'dist\parsers' }  # toolkit or repository, as build-parser.ps1
$zip = Join-Path $outRoot "$($k.id)-$($k.version).zip"
Write-Host ''
Write-Host "Done. Upload $zip on the parser request page (Deliver parser)." -ForegroundColor Green
if (-not $Yes) { Start-Process explorer.exe "/select,`"$zip`"" }
