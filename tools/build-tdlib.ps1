# Builds TDLib (tdjson.dll) from the official sources and places it, with its OpenSSL/zlib runtime DLLs
# and license texts, in third_party\tdlib. Pipkite.Engine copies that folder next to the executable
# on every build, and installer\package.ps1 ships it.
#
#   powershell -ExecutionPolicy Bypass -File tools\build-tdlib.ps1 [-TdRef <tag|commit>] [-Work <dir>]
#
# Needs: Visual Studio 2026 (C++ workload), git. vcpkg is cloned into the work folder if missing.
# The first build takes a long time (OpenSSL + TDLib); later runs reuse the work folder.
param(
    [string]$TdRef = "master",
    [string]$Work = "$env:LOCALAPPDATA\PipkiteBuild"
)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$out = Join-Path $repo "third_party\tdlib"
$vs = "C:\Program Files\Microsoft Visual Studio\18\Community"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) { $cmake = "cmake" }

function Run([string]$exe, [string[]]$argv) {
    Write-Host "> $exe $($argv -join ' ')"
    & $exe @argv
    if ($LASTEXITCODE -ne 0) { throw "$exe failed with exit code $LASTEXITCODE" }
}

New-Item -ItemType Directory -Force $Work | Out-Null
$vcpkg = Join-Path $Work "vcpkg"
if (-not (Test-Path "$vcpkg\vcpkg.exe")) {
    if (-not (Test-Path $vcpkg)) { Run git @("clone", "--depth", "1", "https://github.com/microsoft/vcpkg", $vcpkg) }
    Run "$vcpkg\bootstrap-vcpkg.bat" @("-disableMetrics")
}
Run "$vcpkg\vcpkg.exe" @("install", "gperf:x64-windows", "openssl:x64-windows", "zlib:x64-windows", "--disable-metrics")

$td = Join-Path $Work "td"
if (-not (Test-Path "$td\.git")) { Run git @("clone", "https://github.com/tdlib/td.git", $td) }
Run git @("-C", $td, "fetch", "--tags", "origin")
Run git @("-C", $td, "checkout", $TdRef)
if ($TdRef -eq "master") { Run git @("-C", $td, "pull", "--ff-only") }
$commit = (& git -C $td rev-parse HEAD).Trim()

$build = Join-Path $td "build"
$prefix = Join-Path $td "tdlib"
Run $cmake @("-S", $td, "-B", $build, "-A", "x64", "-DCMAKE_INSTALL_PREFIX:PATH=$prefix",
    "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=$vcpkg\scripts\buildsystems\vcpkg.cmake", "-DVCPKG_TARGET_TRIPLET=x64-windows")
Run $cmake @("--build", $build, "--target", "install", "--config", "Release", "--parallel")

# Stage: tdjson.dll + runtime dependencies + licenses.
$bin = Join-Path $out "bin"
if (Test-Path $out) { Remove-Item -Recurse -Force $out }
New-Item -ItemType Directory -Force $bin, "$out\licenses" | Out-Null
Copy-Item "$prefix\bin\tdjson.dll" $bin
$deps = "$vcpkg\installed\x64-windows\bin"
foreach ($d in "libcrypto-3-x64.dll", "libssl-3-x64.dll", "z.dll") { Copy-Item "$deps\$d" $bin }
Copy-Item "$td\LICENSE_1_0.txt" "$out\licenses\TDLib-LICENSE_1_0.txt"
Copy-Item "$vcpkg\installed\x64-windows\share\openssl\copyright" "$out\licenses\OpenSSL-LICENSE.txt"
Copy-Item "$vcpkg\installed\x64-windows\share\zlib\copyright" "$out\licenses\zlib-LICENSE.txt"
$tdVersion = (Select-String -Path "$td\CMakeLists.txt" -Pattern 'project\(TDLib VERSION ([0-9.]+)').Matches[0].Groups[1].Value
@"
TDLib $tdVersion (commit $commit), built $(Get-Date -Format yyyy-MM-dd) with Visual Studio 2026, x64 Release.
Runtime dependencies from vcpkg (x64-windows): OpenSSL 3 (libcrypto/libssl), zlib.
Rebuild: powershell -ExecutionPolicy Bypass -File tools\build-tdlib.ps1 -TdRef $commit
"@ | Set-Content -Encoding utf8 "$out\VERSION.txt"
Write-Host "TDLib $tdVersion ($commit) staged in $out"
