# Builds Release and lays out the Workshop mod in dist\<name>\ (mod\ + native\<name>.dll), with the DLL's SHA-256 for
# release notes in dist\<name>.sha256.txt. The Workshop page text is mod\mod.xml's description. To publish, run
# install.ps1 (the same layout in %LOCALAPPDATA%\KillHouseGames\DoorKickers2\mods_upload\<name>) and upload that from
# the game's Mods menu. After the first upload, copy the ugcId the game wrote into that mod.xml to mod\mod.xml.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent # the project root, one up from tools\
$name = [regex]::Match((Get-Content (Join-Path $root 'CMakeLists.txt') -Raw), 'project\((\w+)').Groups[1].Value

& (Join-Path $PSScriptRoot 'build.ps1')
$dist = Join-Path $root 'dist'
$mod = Join-Path $dist $name
if (Test-Path $mod) { Remove-Item -LiteralPath $mod -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $mod 'native') | Out-Null
Copy-Item (Join-Path $root 'mod\*') $mod -Recurse
$dll = Join-Path $mod "native\$name.dll"
Copy-Item (Join-Path $root "build\$name.dll") $dll

$hash = '{0}  {1}' -f (Get-FileHash $dll -Algorithm SHA256).Hash.ToLower(), (Split-Path $dll -Leaf)
Set-Content (Join-Path $dist "$name.sha256.txt") $hash
Write-Host "Mod folder: $mod"
Write-Host "  $hash"

# An uploaded item's id lives in mods_upload until it's copied back; without it the next upload publishes a new item.
$uploaded = Join-Path $env:LOCALAPPDATA "KillHouseGames\DoorKickers2\mods_upload\$name\mod.xml"
$id = if (Test-Path $uploaded) { [regex]::Match((Get-Content $uploaded -Raw), 'ugcId="(\d+)"').Groups[1].Value }
if ($id -and (Get-Content (Join-Path $root 'mod\mod.xml') -Raw) -notmatch "ugcId=`"$id`"") {
    Write-Warning "mods_upload\$name\mod.xml has Workshop id $id; put ugcId=`"$id`" in mod\mod.xml."
}
