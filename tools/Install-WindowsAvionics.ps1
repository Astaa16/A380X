param(
    [Parameter(Mandatory=$true)][string]$AircraftDirectory,
    [Parameter(Mandatory=$true)][string]$XPlaneDirectory,
    [switch]$CheckOnly
)
$ErrorActionPreference = "Stop"
$Python = Join-Path $PSScriptRoot "plugins\A380XAvionics\win_x64\python\python.exe"
$Installer = Join-Path $PSScriptRoot "install_windows_avionics.py"
if (-not (Test-Path -LiteralPath $Python)) { throw "Extract the complete Windows package first." }
$Arguments = @("-I", "-B", $Installer, "--package", $PSScriptRoot, "--aircraft", $AircraftDirectory, "--xplane", $XPlaneDirectory)
if ($CheckOnly) { $Arguments += "--check-only" }
& $Python @Arguments
if ($LASTEXITCODE -ne 0) { throw "Windows avionics installation failed. See the message above; do not launch the aircraft with a partial package." }
