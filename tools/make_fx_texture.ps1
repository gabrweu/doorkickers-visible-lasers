# The light's beam cone sprite, into mod/textures/fx as an uncompressed 8-bit luminance DDS with mipmaps. A soft white
# wedge, apex at the left edge's middle (u = 0). The light's real edge is at 1 / $overscan of the wedge, the falloff
# running on to the texture's edge (Lights.cpp's kConeOverscan widens the sprite to match).
# The game loads 8-bit DDS as GL_LUMINANCE, sampled as (L, L, L, 1), and add_masked blends (SRC_ALPHA, ONE): a 4-channel
# texture with grey = alpha = a added a², so L holds a². GL_UNPACK_ALIGNMENT comes from the top level's rows (8), so the
# 4x4 and 2x2 levels are read with padded rows: they and the 1x1 are one flat value, which reads the same either way.
# -PreviewDir also writes a PNG of the top level.
param([string]$PreviewDir)

$ErrorActionPreference = 'Stop'

$size = 256
$overscan = 1.3                              # kConeOverscan in Lights.cpp
$tanHalf = [Math]::Tan(25.0 * [Math]::PI / 180.0) # the default 50 degree cone, for the rounded far end
$outDir = Join-Path $PSScriptRoot '..\mod\textures\fx'

function Smoothstep([double]$edge0, [double]$edge1, [double]$x) {
    $t = [Math]::Min(1.0, [Math]::Max(0.0, ($x - $edge0) / ($edge1 - $edge0)))
    return $t * $t * (3 - 2 * $t)
}

# 0..1 at (x, y), y from the top
function Alpha([int]$x, [int]$y) {
    $t = ($x + 0.5) / $size                              # along: 0 at the apex
    $s = [Math]::Abs(($y + 0.5) / $size * 2.0 - 1.0)     # across: 0 in the middle, 1 at the edges
    if ($s -ge $t) { return 0.0 }
    $q = $s / $t
    $side = [Math]::Pow(1.0 - $q * $q, 2)                # a bell, flat in the middle and at the edge
    $r = $t * [Math]::Sqrt(1.0 + [Math]::Pow($q * $overscan * $tanHalf, 2)) # distance from the lens
    $along = [Math]::Pow(1.0 - [Math]::Min($r, 1.0), 1.6)
    $tip = Smoothstep 0.0 0.06 $t                        # no hard point at the lens
    return $side * $along * $tip
}

# Every level of L (alpha squared), top first, each a [double[]] of rows from the top. Box-filtered down to 1x1; the
# levels of 4x4 and smaller hold their mean.
function Get-Levels {
    $level = New-Object 'double[]' ($size * $size)
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) { $level[$y * $size + $x] = [Math]::Pow((Alpha $x $y), 2) }
    }
    $levels = @(, $level)
    $n = $size
    while ($n -gt 1) {
        $half = $n / 2
        $next = New-Object 'double[]' ($half * $half)
        for ($y = 0; $y -lt $half; $y++) {
            for ($x = 0; $x -lt $half; $x++) {
                $i = 2 * $y * $n + 2 * $x
                $next[$y * $half + $x] = ($level[$i] + $level[$i + 1] + $level[$i + $n] + $level[$i + $n + 1]) / 4.0
            }
        }
        if ($half -le 4) {
            $mean = ($next | Measure-Object -Average).Average
            for ($i = 0; $i -lt $next.Length; $i++) { $next[$i] = $mean }
        }
        $levels += , $next
        $level = $next
        $n = $half
    }
    return $levels
}

# Uncompressed 8-bit luminance DDS, bottom-up rows.
function Save-Dds($path, $levels) {
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([Text.Encoding]::ASCII.GetBytes('DDS '))
    $bw.Write([uint32]124)                  # header size
    $bw.Write([uint32]0x2100F)              # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT | MIPMAPCOUNT
    $bw.Write([uint32]$size); $bw.Write([uint32]$size)
    $bw.Write([uint32]$size)                # pitch
    $bw.Write([uint32]0); $bw.Write([uint32]$levels.Count) # depth, mipmap count
    for ($i = 0; $i -lt 11; $i++) { $bw.Write([uint32]0) } # reserved
    $bw.Write([uint32]32); $bw.Write([uint32]0x20000) # pixel format: size, DDPF_LUMINANCE
    $bw.Write([uint32]0); $bw.Write([uint32]8)        # fourcc, bit count
    $bw.Write([uint32]0xFF); $bw.Write([uint32]0); $bw.Write([uint32]0); $bw.Write([uint32]0) # masks
    $bw.Write([uint32]0x401008)             # caps: COMPLEX | TEXTURE | MIPMAP
    for ($i = 0; $i -lt 4; $i++) { $bw.Write([uint32]0) }
    $n = $size
    foreach ($level in $levels) {
        for ($y = $n - 1; $y -ge 0; $y--) {
            for ($x = 0; $x -lt $n; $x++) { $bw.Write([byte][Math]::Round(255.0 * $level[$y * $n + $x])) }
        }
        $n = [Math]::Max(1, $n / 2)
    }
    $bw.Flush()
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

$levels = Get-Levels
New-Item -ItemType Directory -Force $outDir | Out-Null
$path = Join-Path $outDir 'dk2ml_vlasers_cone.dds'
Save-Dds $path $levels
Write-Host "Wrote $path ($($levels.Count) levels)"

if ($PreviewDir) {
    Add-Type -AssemblyName System.Drawing
    $preview = New-Object Drawing.Bitmap $size, $size
    for ($y = 0; $y -lt $size; $y++) {
        for ($x = 0; $x -lt $size; $x++) {
            $v = 0x20 + [int][Math]::Round($levels[0][$y * $size + $x] * (255 - 0x20))
            $preview.SetPixel($x, $y, [Drawing.Color]::FromArgb($v, $v, $v))
        }
    }
    $preview.Save((Join-Path $PreviewDir 'fx_cone_preview.png'))
    $preview.Dispose()
}
