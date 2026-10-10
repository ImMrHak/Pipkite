<#
.SYNOPSIS
  Free self-signed Authenticode signing so Windows Security (SmartScreen / Smart App Control) does not block
  the app. Signs every unsigned binary in a folder with a persistent self-signed code-signing certificate,
  and exports the PUBLIC certificate so the installer can add it to each user's trust store.

.DESCRIPTION
  - A persistent code-signing cert "CN=Pipkite" is created once in Cert:\CurrentUser\My and reused by
    thumbprint on later runs, so the publisher identity (and therefore the trust the installer establishes)
    stays stable across builds.
  - Microsoft-signed files (VC++ runtime) keep their real signatures; only files that are not already validly
    signed get our signature. Signatures are RFC-3161 timestamped so they stay valid after the cert expires.
  - The private key never leaves this machine / the CurrentUser store; only the public .cer is exported.

  Limitation: a self-signed cert is trusted only where its public cert is installed (the installer does this
  per-user, no admin needed). For unconditional trust on any PC you still need an EV certificate or Store
  distribution; this free route covers your machines and anyone who installs via the provided installer.
.PARAMETER Dir
  Folder whose binaries (recursively) are signed.
.PARAMETER ExportCer
  Optional path to write the public certificate (.cer) to (for the installer to ship).
#>
param(
    [Parameter(Mandatory = $true)][string]$Dir,
    [string]$ExportCer,
    [switch]$Timestamp  # add an RFC-3161 timestamp (needs internet; off by default so signing never hangs)
)
$ErrorActionPreference = "Stop"
$subject = "CN=Pipkite"
$ts = "http://timestamp.digicert.com"

# 1) Get-or-create the persistent signing cert.
$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject -and $_.HasPrivateKey } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (-not $cert) {
    Write-Host "Creating persistent self-signed code-signing certificate ($subject)..."
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation Cert:\CurrentUser\My `
        -KeyUsage DigitalSignature -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(10) `
        -FriendlyName "Pipkite code signing (self-signed)"
}
Write-Host "Signing with cert thumbprint $($cert.Thumbprint)"

# 2) Trust it for the current user so signatures verify on this machine (Root + TrustedPublisher).
foreach ($store in "Root", "TrustedPublisher") {
    if (-not (Get-ChildItem "Cert:\CurrentUser\$store" | Where-Object { $_.Thumbprint -eq $cert.Thumbprint })) {
        $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($store, "CurrentUser")
        $s.Open("ReadWrite"); $s.Add([System.Security.Cryptography.X509Certificates.X509Certificate2]$cert); $s.Close()
    }
}

# 3) Sign every binary that isn't already validly signed (keeps Microsoft's VC runtime signatures intact).
#    EXCLUDE the hot-reloadable modules under bridge\impl and plugins: those are verified by their own
#    manifest.json SHA-256 + ECDSA signature, so Authenticode-signing them would change their bytes and break
#    that hash ("no active implementation recorded"). They load fine unsigned under Smart App Control.
$signed = 0; $skipped = 0; $failed = 0
Get-ChildItem -Path $Dir -Recurse -Include *.exe, *.dll -File | ForEach-Object {
    $f = $_.FullName
    if ($f -match '\\(bridge\\impl|plugins)\\') { $skipped++; return }
    if ((Get-AuthenticodeSignature $f).Status -eq 'Valid') { $skipped++; return }
    if ($Timestamp) {
        try { $r = Set-AuthenticodeSignature -FilePath $f -Certificate $cert -HashAlgorithm SHA256 -TimestampServer $ts -ErrorAction Stop }
        catch { $r = Set-AuthenticodeSignature -FilePath $f -Certificate $cert -HashAlgorithm SHA256 }
    } else {
        # No timestamp: fast and offline-safe. The self-signed cert is valid for 10 years, so an untimestamped
        # signature stays valid for the life of the build. (Enable -Timestamp with a real cert for production.)
        $r = Set-AuthenticodeSignature -FilePath $f -Certificate $cert -HashAlgorithm SHA256
    }
    if ($r.Status -eq 'Valid') { $signed++ } else { $failed++; Write-Warning "sign failed ($($r.Status)): $f" }
}
Write-Host "Signed $signed, skipped $skipped already-valid, failed $failed."
if ($failed -gt 0) { throw "$failed file(s) failed to sign" }

# 4) Export the public certificate for the installer to trust on end-user machines.
if ($ExportCer) {
    Export-Certificate -Cert $cert -FilePath $ExportCer -Type CERT | Out-Null
    Write-Host "Public certificate exported to $ExportCer"
}
