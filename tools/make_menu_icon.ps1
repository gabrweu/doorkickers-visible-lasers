# The Esc-menu settings icon, in the stock menu icons' style and format (64x64 uncompressed 24-bit DDS), into
# mod/textures/gui. Adapted from Free Camera's make_freecam_icon.ps1. -PreviewDir also writes a PNG preview.
param([string]$PreviewDir)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$cream = [Drawing.Color]::FromArgb(0xf0, 0xe3, 0xcc)
$dark = [Drawing.Color]::FromArgb(0x40, 0x39, 0x2b)
$size = 64
$scale = 4 # drawn at 256x256 and scaled down, for smooth edges like the stock icons
$outDir = Join-Path $PSScriptRoot '..\mod\textures\gui'

function Draw-Icon($background, $glyph) {
    $s = $size * $scale
    $big = New-Object Drawing.Bitmap $s, $s
    $g = [Drawing.Graphics]::FromImage($big)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear($background)
    $fg = New-Object Drawing.SolidBrush $glyph
    $bg = New-Object Drawing.SolidBrush $background
    function Px($v) { [single]($v * $scale) } # coordinates below are in 64x64 icon pixels, around the icon's center

    # the HUD toggle's glyph, bigger: a laser module firing a beam up and to the right that ends in a dot
    $g.TranslateTransform((Px 32), (Px 32))
    $g.RotateTransform(-35)

    $body = New-Object Drawing.Drawing2D.GraphicsPath
    $x = Px (-27); $y = Px (-8); $w = Px 19; $h = Px 16; $r = Px 4
    $body.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $body.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $body.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $body.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $body.CloseFigure()
    $g.FillPath($fg, $body)
    $g.FillRectangle($fg, (Px (-8.5)), (Px (-4.5)), (Px 4), (Px 9))
    $g.FillEllipse($bg, (Px (-21)), (Px (-3)), (Px 6), (Px 6)) # a button on the module

    $pen = New-Object Drawing.Pen $glyph, (Px 4)
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $g.DrawLine($pen, (Px (-1)), 0, (Px 17), 0)
    $g.FillEllipse($fg, (Px 19), (Px (-6.5)), (Px 13), (Px 13))
    $g.ResetTransform()

    $small = New-Object Drawing.Bitmap $size, $size
    $gs = [Drawing.Graphics]::FromImage($small)
    $gs.InterpolationMode = 'HighQualityBicubic'
    $gs.PixelOffsetMode = 'HighQuality'
    $gs.DrawImage($big, 0, 0, $size, $size)
    $g.Dispose(); $gs.Dispose(); $big.Dispose()
    return $small
}

# Uncompressed 24-bit RGB DDS, top-down rows, BGR byte order (masks R=0xFF0000 G=0xFF00 B=0xFF), no mipmaps.
function Save-Dds($bitmap, $path) {
    $w = $bitmap.Width; $h = $bitmap.Height
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([Text.Encoding]::ASCII.GetBytes('DDS '))
    $bw.Write([uint32]124)                  # header size
    $bw.Write([uint32]0x100F)               # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    $bw.Write([uint32]$h); $bw.Write([uint32]$w)
    $bw.Write([uint32]($w * 3))             # pitch
    $bw.Write([uint32]0); $bw.Write([uint32]0) # depth, mipmap count
    for ($i = 0; $i -lt 11; $i++) { $bw.Write([uint32]0) } # reserved
    $bw.Write([uint32]32); $bw.Write([uint32]0x40) # pixel format: size, DDPF_RGB
    $bw.Write([uint32]0); $bw.Write([uint32]24)    # fourcc, bit count
    $bw.Write([uint32]0x00FF0000); $bw.Write([uint32]0x0000FF00); $bw.Write([uint32]0x000000FF); $bw.Write([uint32]0)
    $bw.Write([uint32]0x1000)               # caps: texture
    for ($i = 0; $i -lt 4; $i++) { $bw.Write([uint32]0) }
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bitmap.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R)
        }
    }
    $bw.Flush()
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

New-Item -ItemType Directory -Force $outDir | Out-Null
$normal = Draw-Icon $cream $dark
$hover = Draw-Icon $dark $cream
Save-Dds $normal (Join-Path $outDir 'dk2ml_vlasers_normal.dds')
Save-Dds $hover (Join-Path $outDir 'dk2ml_vlasers_hover.dds')
Write-Host "Wrote $outDir\dk2ml_vlasers_normal.dds and dk2ml_vlasers_hover.dds"

if ($PreviewDir) {
    $sheet = New-Object Drawing.Bitmap 520, 256
    $g = [Drawing.Graphics]::FromImage($sheet)
    $g.InterpolationMode = 'NearestNeighbor'
    $g.DrawImage($normal, 0, 0, 256, 256)
    $g.DrawImage($hover, 264, 0, 256, 256)
    $sheet.Save((Join-Path $PreviewDir 'menu_icon_preview.png'))
    $g.Dispose()
}
