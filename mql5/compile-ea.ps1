<#
.SYNOPSIS
  Compiles SignalRelayEA.mq5 with MetaEditor from an installed MetaTrader 5 terminal.
.PARAMETER MetaEditor
  Path to MetaEditor64.exe (auto-detected under Program Files when omitted).
.PARAMETER OutDir
  Optional folder that receives SignalRelayEA.ex5 (e.g. x64\Release\mql5).
#>
param([string]$MetaEditor = "", [string]$OutDir = "")
$ErrorActionPreference = 'Stop'
$src = Join-Path $PSScriptRoot 'SignalRelayEA\SignalRelayEA.mq5'
if (-not $MetaEditor) {
    $MetaEditor = Get-ChildItem "$env:ProgramFiles\*\MetaEditor64.exe", "$env:ProgramFiles\*\*\MetaEditor64.exe" -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $MetaEditor -or -not (Test-Path $MetaEditor)) { throw "MetaEditor64.exe not found. Install MetaTrader 5 or pass -MetaEditor." }
$work = Join-Path $env:TEMP "SignalRelayEA-build"
New-Item -ItemType Directory -Force $work | Out-Null
Copy-Item $src $work -Force
$log = Join-Path $work 'compile.log'
& $MetaEditor "/compile:$work\SignalRelayEA.mq5" "/log:$log" | Out-Null
Start-Sleep -Milliseconds 500
$text = Get-Content $log -Encoding Unicode
$text | Where-Object { $_ -match 'error|warning|Result' } | ForEach-Object { Write-Host $_ }
if (-not ($text -match 'Result: 0 errors')) { throw "EA compilation failed" }
if ($OutDir) { New-Item -ItemType Directory -Force $OutDir | Out-Null; Copy-Item "$work\SignalRelayEA.ex5" $OutDir -Force; Write-Host "Copied SignalRelayEA.ex5 to $OutDir" }
