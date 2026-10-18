# Builds the plugin DLL (MSVC x64) into build\. -Config: CMake build type (default Release, the one for the Workshop).
# Needs Visual Studio 2022 or its Build Tools with the C++ workload (MSVC, CMake, Ninja).
param([string]$Config = 'Release')

$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 build tools not found' }

$src = Split-Path $PSScriptRoot -Parent # the project root, one up from tools\
$build = Join-Path $src 'build'
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH" # vcvars calls vswhere by name

cmd /c "`"$vcvars`" >nul && cmake -S `"$src`" -B `"$build`" -G Ninja -DCMAKE_BUILD_TYPE=$Config && cmake --build `"$build`""
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
