$ErrorActionPreference = "Stop"
$Python = Join-Path $PSScriptRoot "plugins\A380XAvionics\win_x64\python\python.exe"
$Checker = Join-Path $PSScriptRoot "install_windows_avionics.py"
if (-not (Test-Path -LiteralPath $Python)) { throw "Extract the complete Windows package first." }
& $Python -I -B $Checker --live-check
if ($LASTEXITCODE -ne 0) { throw "Live display check failed. Enable the X-Plane Web API, load and power A380X-XP12, and inspect both logs." }
