# Dry-runs the plugin with symtest, then copies mod\ + the DLL (native\) to the game's mods_upload\<name>\. Only adds
# and overwrites files. Close the game first: it locks the DLL.
#   -GameDir  the game folder
#   -SymTest  symtest.exe; default ..\doorkickers-modloader\build\symtest.exe. Runs in build\, so its log lands there.
param(
    [string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\DoorKickers2',
    [string]$SymTest
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$name = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\((\w+)').Groups[1].Value
$dll = Join-Path $root "build\$name.dll"
if (-not (Test-Path $dll)) { throw 'Build first: .\tools\build.ps1' }
if (Get-Process DoorKickers2 -ErrorAction SilentlyContinue) { throw 'Close Door Kickers 2 first (it locks the DLL).' }

if (-not $SymTest) {
    $candidate = Join-Path $root '..\doorkickers-modloader\build\symtest.exe'
    if (Test-Path $candidate) { $SymTest = $candidate }
}
if ($SymTest) {
    # Exit code 0: init returned 0 and the loader would refuse nothing. 100: a refused call (see the PROBLEM lines).
    Push-Location (Split-Path $dll)
    try { $out = & $SymTest $GameDir $dll } finally { Pop-Location }
    if ($LASTEXITCODE -ne 0) { $out | Write-Host; throw "symtest dry run failed (exit code $LASTEXITCODE)" }
    Write-Host 'symtest: dry run clean'
} else {
    Write-Warning 'symtest.exe not found (pass -SymTest): skipping the dry run'
}

$target = Join-Path $env:LOCALAPPDATA "KillHouseGames\DoorKickers2\mods_upload\$name"
New-Item -ItemType Directory -Force (Join-Path $target 'native') | Out-Null
Get-ChildItem (Join-Path $root 'mod') | Where-Object Name -ne 'mod.xml' | Copy-Item -Destination $target -Recurse -Force

# Publishing writes the item's Workshop id into the target's mod.xml (ugcId). Without it, or with another one, the
# Mods menu offers to publish a new item instead of updating, so such a mod.xml is never replaced.
function Get-UgcId($path) {
    if (-not (Test-Path -LiteralPath $path)) { return $null }
    $m = [regex]::Match((Get-Content -LiteralPath $path -Raw), 'ugcId="(\d+)"')
    if ($m.Success) { $m.Groups[1].Value } else { $null }
}
$repoModXml = Join-Path $root 'mod\mod.xml'
$targetModXml = Join-Path $target 'mod.xml'
$publishedId = Get-UgcId $targetModXml
if ($publishedId -and $publishedId -ne (Get-UgcId $repoModXml)) {
    Write-Warning "mods_upload\$name\mod.xml kept: it holds Workshop id $publishedId. Put ugcId=`"$publishedId`" in mod\mod.xml."
} else {
    Copy-Item $repoModXml $targetModXml -Force
}
Copy-Item $dll (Join-Path $target 'native') -Force

$missing = 'dk2ml.dll', 'dbghelp.dll' | Where-Object { -not (Test-Path (Join-Path $GameDir $_)) }
if ($missing) { Write-Warning "The Modloader isn't installed in $GameDir (missing: $($missing -join ', '))." }
Write-Host "Installed to $target. Enable 'era's Lights and Lasers' in the in-game Mods menu; the loader logs to $GameDir\dk2ml.log."
