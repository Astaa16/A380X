param(
    [Parameter(Mandatory=$true)][string]$XPlaneDir,
    [Parameter(Mandatory=$true)][string]$WindowsPackage,
    [string]$AircraftDir,
    [switch]$CheckOnly
)
$ErrorActionPreference = "Stop"
if (-not $AircraftDir) { $AircraftDir = Join-Path $XPlaneDir "Aircraft\A380X-XP12" }
$Installer = Join-Path $WindowsPackage "Install-WindowsAvionics.ps1"
if (-not (Test-Path -LiteralPath $Installer)) {
    throw "Extract the complete Windows plugin kit and pass its directory with -WindowsPackage. No files were changed."
}
& $Installer -AircraftDirectory $AircraftDir -XPlaneDirectory $XPlaneDir -CheckOnly:$CheckOnly
# Required aileron/hydraulic Lua drivers are installed by that validated kit.
# Never silently copy legacy FBW/avionics/FCU controllers over the native systems.
