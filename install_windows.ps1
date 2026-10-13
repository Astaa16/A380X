param(
    [Parameter(Mandatory = $true)][string]$XPlaneDir
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = Join-Path $ProjectDir "FlyWithLua\Scripts"
$ScriptsDir = Join-Path $XPlaneDir "Resources\plugins\FlyWithLua\Scripts"

if (-not (Test-Path $ScriptsDir)) {
    throw "FlyWithLua Scripts was not found under $XPlaneDir"
}

foreach ($Filename in @("A380X_XP12_FBW.lua")) {
    Copy-Item -Force (Join-Path $SourceDir $Filename) (Join-Path $ScriptsDir $Filename)
    Write-Host "Installed $Filename"
}
Write-Host "PFD/ND/flyPad avionics are aircraft-local native plugins; legacy Lua display/FCU adapters are not installed."
