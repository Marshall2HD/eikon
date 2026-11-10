# Installs Eikon from this folder: trusts the certificate the package is signed with, installs the
# Windows App SDK runtime packages it depends on, then installs (or updates) Eikon and starts it.
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$cer = Join-Path $here 'Eikon.cer'
$cert = [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($cer)

if (-not (Get-ChildItem Cert:\LocalMachine\TrustedPeople | Where-Object Thumbprint -eq $cert.Thumbprint)) {
    $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
    $import = "Import-Certificate -FilePath '$cer' -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null"
    if ($admin) {
        Invoke-Expression $import
    } else {
        Write-Host "Windows will ask for permission to trust Eikon's signing certificate."
        Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList '-NoProfile', '-Command', $import
    }
    if (-not (Get-ChildItem Cert:\LocalMachine\TrustedPeople | Where-Object Thumbprint -eq $cert.Thumbprint)) {
        throw "The signing certificate was not trusted, so Windows won't install the package."
    }
}

$deps = Join-Path $here 'Dependencies'
if (Test-Path $deps) {
    Get-ChildItem $deps -Recurse -Include *.msix, *.appx | ForEach-Object {
        # 0x80073D06: the same or a newer version is already installed, which is fine.
        try { Add-AppxPackage -Path $_.FullName -ErrorAction Stop }
        catch { if ($_.Exception.HResult -ne -2147009274 -and "$_" -notmatch '0x80073D06') { throw } }
    }
}

$msix = Get-ChildItem $here -Filter 'Eikon_*.msix' | Select-Object -First 1
Add-AppxPackage -Path $msix.FullName -ForceApplicationShutdown -ForceUpdateFromAnyVersion
$family = (Get-AppxPackage -Name Eikon).PackageFamilyName
Write-Host "Eikon is installed."
Start-Process "shell:AppsFolder\$family!App"
