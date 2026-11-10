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
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Get-ChildItem $deps -Recurse -Include *.msix, *.appx | ForEach-Object {
        $file = $_.FullName
        try { Add-AppxPackage -Path $file -ErrorAction Stop }
        catch {
            $message = "$_"
            # 0x80073D06: the same or a newer version is installed.
            if ($_.Exception.HResult -eq -2147009274 -or $message -match '0x80073D06') { return }
            # 0x80073D02: an older version is installed and other apps are using it. Eikon only needs a
            # minimum version, which the installed one normally meets; if it doesn't, installing Eikon
            # below says so.
            if ($message -match '0x80073D02') {
                $zip = [System.IO.Compression.ZipFile]::OpenRead($file)
                try {
                    $reader = [System.IO.StreamReader]::new($zip.GetEntry('AppxManifest.xml').Open())
                    $name = ([xml]$reader.ReadToEnd()).Package.Identity.Name
                    $reader.Dispose()
                } finally { $zip.Dispose() }
                if (Get-AppxPackage -Name $name) { return }
            }
            throw
        }
    }
}

$msix = Get-ChildItem $here -Filter 'Eikon_*.msix' | Select-Object -First 1
Add-AppxPackage -Path $msix.FullName -ForceApplicationShutdown -ForceUpdateFromAnyVersion
$family = (Get-AppxPackage -Name Eikon).PackageFamilyName
Write-Host "Eikon is installed."
Start-Process "shell:AppsFolder\$family!App"
