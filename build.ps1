# build.ps1 — build Ramp.aex (Release x64) and the color_math unit test.
#   .\build.ps1            # build plugin
#   .\build.ps1 -Test      # also build & run color_math_test
#   .\build.ps1 -Config Debug
param(
    [string]$Config = "Release",
    [string]$SdkPath = "",        # path to the AE SDK root (folder with Examples\Headers); overrides project default
    [switch]$Test
)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
if ($SdkPath) { $env:AE_SDK_PATH = $SdkPath }

# Locate Visual Studio (vcvars64) via vswhere.
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw "Visual Studio with C++ tools not found." }
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"

# Detect the installed v14x platform toolset (this machine ships v145).
$toolsetRoot = Join-Path $vs "MSBuild\Microsoft\VC"
$toolset = "v143"
$found = Get-ChildItem -Path $toolsetRoot -Recurse -Directory -Filter "v14*" -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -match "PlatformToolsets" } | Select-Object -First 1
if ($found) { $toolset = $found.Name }

$proj = Join-Path $root "src\win\Ramp.vcxproj"
$pluginDir = Join-Path $root "build\plugins"
New-Item -ItemType Directory -Force $pluginDir | Out-Null

Write-Host "VS:       $vs"
Write-Host "Toolset:  $toolset"
Write-Host "Config:   $Config|x64"
Write-Host "Output:   $pluginDir"

$env:AE_PLUGIN_BUILD_DIR = $pluginDir
$cmd = "call `"$vcvars`" >nul 2>&1 && msbuild `"$proj`" /p:Configuration=$Config /p:Platform=x64 /p:PlatformToolset=$toolset /v:minimal /nologo"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { throw "Plugin build FAILED ($LASTEXITCODE)." }
Write-Host "`nBuilt: $(Join-Path $pluginDir 'Ramp.aex')" -ForegroundColor Green

if ($Test) {
    $exe = Join-Path $root "build\color_math_test.exe"
    $tcmd = "call `"$vcvars`" >nul 2>&1 && cl /nologo /EHsc /O2 /I `"$root\src`" /Fe`"$exe`" /Fo`"$root\build\color_math_test.obj`" `"$root\test\color_math_test.cpp`" && `"$exe`""
    cmd /c $tcmd
    if ($LASTEXITCODE -ne 0) { throw "Color tests FAILED." }
}
