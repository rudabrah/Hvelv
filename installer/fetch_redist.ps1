# Laster ned ODBC Driver 18 for SQL Server (x64) til installer\redist\. Hvelv-Setup.exe installerer
# den bare på PC-er som mangler både driver 17 og 18. Filen sjekkes ikke inn i git.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$redist = Join-Path $PSScriptRoot 'redist'
New-Item -ItemType Directory -Force $redist | Out-Null

# ODBC Driver 18.7 trenger ikke VC++ Redistributable på forhånd.
# Kilde: https://learn.microsoft.com/sql/connect/odbc/download-odbc-driver-for-sql-server
$odbc = Join-Path $redist 'msodbcsql.msi'
if (Test-Path $odbc) {
    Write-Host "Finnes allerede: $odbc (slett den for å hente nyeste versjon)"
} else {
    Invoke-WebRequest -Uri 'https://go.microsoft.com/fwlink/?linkid=2378279' -OutFile $odbc -UseBasicParsing
}

$sig = Get-AuthenticodeSignature $odbc
if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
    Remove-Item $odbc
    throw "$odbc er ikke gyldig signert av Microsoft ($($sig.Status))."
}
Write-Host "OK (signert av Microsoft): $odbc"
