# install.ps1 — copy Ramp.aex into the After Effects plug-ins folder.
# Writing under "C:\Program Files\..." needs admin; the script self-elevates.
param(
    [string]$AeVersion = "2026"
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$src  = Join-Path $root "build\plugins\Ramp.aex"
if (-not (Test-Path $src)) { throw "Ramp.aex not found. Run .\build.ps1 first." }

$destDir = "C:\Program Files\Adobe\Adobe After Effects $AeVersion\Support Files\Plug-ins\KPX"

# Self-elevate if we can't write to Program Files.
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
            ).IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Elevating to copy into Program Files..."
    Start-Process pwsh -Verb RunAs -ArgumentList @(
        "-NoProfile","-File",$PSCommandPath,"-AeVersion",$AeVersion) -Wait
    return
}

New-Item -ItemType Directory -Force $destDir | Out-Null
Copy-Item $src (Join-Path $destDir "Ramp.aex") -Force
Write-Host "Installed -> $destDir\Ramp.aex" -ForegroundColor Green
Write-Host "Restart After Effects; effect appears under Effect > Generate > Ramp."
