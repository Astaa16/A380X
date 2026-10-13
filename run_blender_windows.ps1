param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [string]$BlenderExe = "C:\Program Files\Blender Foundation\Blender 4.5\blender.exe",
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$ProjectDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $OutputPath) {
    $OutputPath = Join-Path $ProjectDir "output\A380X-XP12\A380X-converted.blend"
}
$ReportPath = Join-Path $ProjectDir "output\A380X-XP12\animation-report.json"

if (-not (Test-Path $BlenderExe)) {
    throw "Blender was not found at $BlenderExe. Pass -BlenderExe with the installed path."
}

& $BlenderExe --background --python-exit-code 1 `
    --python (Join-Path $ProjectDir "blender\convert_gltf_animations.py") -- `
    --input $InputPath `
    --mapping (Join-Path $ProjectDir "config\animation_datarefs.json") `
    --output $OutputPath `
    --report $ReportPath

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
