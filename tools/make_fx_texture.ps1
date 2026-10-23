# The light's beam cone sprite, into mod/textures/fx as uncompressed 32-bit BGRA DDS. A soft white wedge, apex at the
# left edge's middle (u = 0). Grey levels match the alpha, so it fades the same with either blend. -PreviewDir also
# writes a PNG.
param([string]$PreviewDir)

$ErrorActionPreference = 'Stop'

$width = 256
$height = 64
$fullUntil = 0.45 # fully bright up to this fraction of the half width
$outDir = Join-Path $PSScriptRoot '..\mod\textures\fx'

function Smoothstep([double]$edge0, [double]$edge1, [double]$x) {
    $t = [Math]::Min(1.0, [Math]::Max(0.0, ($x - $edge0) / ($edge1 - $edge0)))
    return $t * $t * (3 - 2 * $t)
}

# 0..255 at (x, y), y from the top
function Alpha([int]$x, [int]$y) {
    $t = ($x + 0.5) / $width                             # along: 0 at the apex
    $s = [Math]::Abs(($y + 0.5) / $height * 2.0 - 1.0)   # across: 0 in the middle, 1 at the edges
    if ($s -ge $t) { return 0 }
    $side = 1.0 - (Smoothstep $fullUntil 1.0 ($s / $t))
    $along = [Math]::Pow(1.0 - $t, 1.6)
    $tip = Smoothstep 0.0 0.06 $t                        # no hard point at the lens
    return [int][Math]::Round(255.0 * $side * $along * $tip)
}

# Uncompressed 32-bit BGRA DDS, bottom-up rows, no mipmaps.
function Save-Dds($path) {
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([Text.Encoding]::ASCII.GetBytes('DDS '))
    $bw.Write([uint32]124)                  # header size
    $bw.Write([uint32]0x100F)               # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    $bw.Write([uint32]$height); $bw.Write([uint32]$width)
    $bw.Write([uint32]($width * 4))         # pitch
    $bw.Write([uint32]0); $bw.Write([uint32]0) # depth, mipmap count
    for ($i = 0; $i -lt 11; $i++) { $bw.Write([uint32]0) } # reserved
    $bw.Write([uint32]32); $bw.Write([uint32]0x41) # pixel format: size, DDPF_RGB | DDPF_ALPHAPIXELS
    $bw.Write([uint32]0); $bw.Write([uint32]32)    # fourcc, bit count
    $bw.Write([uint32]0x00FF0000); $bw.Write([uint32]0x0000FF00); $bw.Write([uint32]0x000000FF)
    $bw.Write([uint32]4278190080)           # alpha mask 0xFF000000 (a hex literal would be a negative int)
    $bw.Write([uint32]0x1000)               # caps: texture
    for ($i = 0; $i -lt 4; $i++) { $bw.Write([uint32]0) }
    for ($y = $height - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $width; $x++) {
            $a = [byte](Alpha $x $y)
            $bw.Write($a); $bw.Write($a); $bw.Write($a); $bw.Write($a)
        }
    }
    $bw.Flush()
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

New-Item -ItemType Directory -Force $outDir | Out-Null
$path = Join-Path $outDir 'dk2ml_vlasers_cone.dds'
Save-Dds $path
Write-Host "Wrote $path"

if ($PreviewDir) {
    Add-Type -AssemblyName System.Drawing
    $preview = New-Object Drawing.Bitmap $width, $height
    for ($y = 0; $y -lt $height; $y++) {
        for ($x = 0; $x -lt $width; $x++) {
            $a = Alpha $x $y
            $v = 0x20 + [int]($a * (255 - 0x20) / 255)
            $preview.SetPixel($x, $y, [Drawing.Color]::FromArgb($v, $v, $v))
        }
    }
    $preview.Save((Join-Path $PreviewDir 'fx_cone_preview.png'))
    $preview.Dispose()
}
