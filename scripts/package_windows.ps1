<#
.SYNOPSIS
  Latex Compiler — Windows packaging (MSI + portable ZIP).

  RUN THIS ON WINDOWS. The app is built with MSVC there; this machine's
  other packaging path (macOS DMG) cannot produce Windows binaries.

.PREREQUISITES (all on PATH, in a normal terminal)
  - CMake 3.30+ and Ninja
  - Visual Studio 2022 Build Tools (MSVC). Easiest: run this script from
    "Developer PowerShell for VS 2022" so cmake finds the compiler.
  - pnpm (frontend build)
  - WiX Toolset v3.14 (MSI generator): https://wixtoolset.org/releases/
    Without it the portable ZIP still builds; only the MSI is skipped.
  - End users need the WebView2 Runtime (preinstalled on updated Win10/11).

.USAGE
  pwsh -ExecutionPolicy Bypass -File scripts/package_windows.ps1              # full build
  pwsh -ExecutionPolicy Bypass -File scripts/package_windows.ps1 -SkipBuild   # reuse outputs

  (-ExecutionPolicy Bypass is needed on default Windows installs, where the
  script execution policy is Restricted; it applies to this run only.)

  Output: dist/Latex Compiler-<version>-windows-<arch>.msi / .zip
#>
param(
    [switch]$SkipBuild,
    [string]$Preset = 'release',
    # ---- Code signing (optional, recommended for distribution) ----
    # Provide EITHER the subject or the thumbprint of a code-signing
    # certificate installed in Cert:\CurrentUser\My (see the guide).
    # When set, the exe is signed BEFORE cpack so both the MSI and the ZIP
    # ship a signed binary, and the MSI itself is signed afterwards.
    [string]$CertificateSubject = '',
    [string]$CertificateThumbprint = '',
    # RFC3161 timestamp server: keeps signatures valid after the certificate
    # expires. Any trusted TSA works; this one is free and reliable.
    [string]$TimestampServer = 'http://timestamp.digicert.com',
    # Validates the whole signing pipeline with a throwaway self-signed
    # certificate. NOT for distribution: other machines will not trust it.
    [switch]$TestSelfSigned
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectRoot = Split-Path -Parent $PSScriptRoot
Push-Location $ProjectRoot
try {
    if (-not $SkipBuild) {
        Write-Host '==> Building (Release)'
        cmake --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }
        cmake --build --preset $Preset
        if ($LASTEXITCODE -ne 0) { throw 'cmake build failed' }
    }

    $BuildDir = Join-Path $ProjectRoot "build/$Preset"
    # Ninja presets are single-config (exe at the build root); VS generators
    # put it under Release/. Accept both.
    $Exe = @(
        (Join-Path $BuildDir 'Latex Compiler.exe'),
        (Join-Path $BuildDir 'Release/Latex Compiler.exe')
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $Exe) {
        throw "executable not found under $BuildDir (build first or drop -SkipBuild)"
    }
    $ExeDir = Split-Path -Parent $Exe

    # Runtime layout check: the binary looks for the single-file frontend
    # next to itself; shipping without it means a blank window.
    if (-not (Test-Path (Join-Path $ExeDir 'frontend/index.html'))) {
        throw "frontend assets missing at $ExeDir/frontend (the bundle-assets target should have copied them)"
    }

    # ---- Code signing setup ----
    $signCert = $null
    if ($TestSelfSigned) {
        Write-Host '==> Creating a self-signed TEST certificate (not for distribution)'
        $signCert = New-SelfSignedCertificate -Subject 'CN=Latex Compiler Test' `
            -Type CodeSigningCert -CertStoreLocation 'Cert:\CurrentUser\My'
    }
    elseif ($CertificateThumbprint -or $CertificateSubject) {
        $store = Get-ChildItem 'Cert:\CurrentUser\My'
        if ($CertificateThumbprint) {
            $signCert = $store | Where-Object { $_.Thumbprint -eq $CertificateThumbprint } | Select-Object -First 1
        }
        else {
            $signCert = $store | Where-Object { $_.Subject -like "*$CertificateSubject*" } | Select-Object -First 1
        }
        if (-not $signCert) {
            throw 'code signing certificate not found in Cert:\CurrentUser\My (check subject/thumbprint, or import your .pfx)'
        }
    }

    function Invoke-Sign([string]$Path) {
        if (-not $signCert) { return }
        Write-Host ("==> Signing {0}" -f (Split-Path -Leaf $Path))
        # signtool (Windows SDK; preferred: SHA-256 + RFC3161 timestamp) when
        # on PATH; Set-AuthenticodeSignature (built-in) otherwise.
        $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
        if ($signtool) {
            & $signtool.Source sign /fd sha256 /td sha256 /tr $TimestampServer `
                /sha1 $signCert.Thumbprint $Path
            if ($LASTEXITCODE -ne 0) { throw "signtool failed on $Path" }
        }
        else {
            $result = Set-AuthenticodeSignature -FilePath $Path -Certificate $signCert `
                -TimestampServer $TimestampServer -HashAlgorithm SHA256
            if ($result.Status -ne 'Valid') { throw "signing failed on $Path ($($result.Status))" }
        }
        Write-Host '    signature OK'
    }

    # Sign the binary BEFORE cpack: the MSI embeds it and the ZIP ships it,
    # so one signature covers both artifacts.
    Invoke-Sign $Exe

    # WiX presence check with actionable guidance (ZIP works without it).
    $wixBin = @(
        "${env:ProgramFiles(x86)}\WiX Toolset v3.14\bin",
        "${env:ProgramFiles}\WiX Toolset v3.14\bin"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $wixBin) {
        Write-Warning 'WiX Toolset v3.14 not found: the MSI will be skipped, the portable ZIP still builds.'
        Write-Warning 'Install from https://wixtoolset.org/releases/ to also get the MSI.'
    }

    Write-Host '==> Packaging (WIX + ZIP via CPack)'
    Push-Location $BuildDir
    try {
        cpack -C Release
        if ($LASTEXITCODE -ne 0) { throw 'cpack failed' }
    }
    finally {
        Pop-Location
    }

    $Dist = Join-Path $ProjectRoot 'dist'
    New-Item -ItemType Directory -Force -Path $Dist | Out-Null
    Get-ChildItem $BuildDir -Filter 'Latex Compiler-*.msi' -ErrorAction SilentlyContinue |
        Move-Item -Destination $Dist -Force
    Get-ChildItem $BuildDir -Filter 'Latex Compiler-*.zip' -ErrorAction SilentlyContinue |
        Move-Item -Destination $Dist -Force

    # The MSI is a separate binary container: it needs its own signature.
    Get-ChildItem $Dist -Filter 'Latex Compiler-*.msi' -ErrorAction SilentlyContinue |
        ForEach-Object { Invoke-Sign $_.FullName }

    Write-Host ''
    $artifacts = Get-ChildItem $Dist -Filter 'Latex Compiler-*'
    if (-not $artifacts) { throw 'no packaged artifacts found; check cpack output above' }
    foreach ($artifact in $artifacts) {
        Write-Host ('OK  {0}  ({1:N1} MB)' -f $artifact.Name, ($artifact.Length / 1MB))
    }
    Write-Host ''
    Write-Host 'Install: run the .msi (Start Menu entry included).'
    Write-Host 'Portable: unzip and run Latex Compiler.exe.'
    if ($signCert) {
        Write-Host 'Builds are signed: SmartScreen shows the verified publisher (reputation still accrues over downloads).'
    }
    else {
        Write-Host 'Note: unsigned builds trigger SmartScreen on first run (More info -> Run anyway).'
        Write-Host '      To sign, run again with -CertificateSubject/-CertificateThumbprint (see docs/windows-packaging-guide.md).'
    }
    Write-Host 'Toast attribution: the code posts with AUMID "Misaint20.latexcompiler"; if toasts do not'
    Write-Host '  appear after install, verify the Start Menu shortcut carries that AppUserModelID.'
}
finally {
    Pop-Location
}
