# The per-trooper laser (visible and IR), light and Behaviors toggles for the right-click menu, in the style of its
# stock top-row toggles (go silent, always wait, speed sync), into mod/textures/hud, as uncompressed 32-bit BGRA DDS
# like the stock textures. Adapted from Free Camera's make_hud_icon.ps1. -PreviewDir also writes a PNG sheet.
param([string]$PreviewDir)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# the right-click menu's top-row toggles (data/textures/gui_ingame/waypoints/go_silent_*.dds)
$menuOff = [Drawing.Color]::FromArgb(0xf8, 0x78, 0x00)
$menuOffGlyph = [Drawing.Color]::FromArgb(0x20, 0x18, 0x18)
$menuOn = [Drawing.Color]::FromArgb(0xf8, 0xa0, 0x00)
$menuOnGlyph = [Drawing.Color]::FromArgb(0xa0, 0x38, 0x00)
$menuHoverRim = [Drawing.Color]::FromArgb(0xf8, 0xc0, 0x00)
# greyed (_none)
$menuNone = [Drawing.Color]::FromArgb(0x80, 0x80, 0x80)
$menuNoneGlyph = [Drawing.Color]::FromArgb(0x40, 0x40, 0x40)
$scale = 4 # drawn at 4x and scaled down, for smooth edges like the stock textures
$outDir = Join-Path $PSScriptRoot '..\mod\textures\hud'

# Toggle geometry, in its 64x64 pixels: a rounded diamond over rows 4-59 like the stock ones
$unitSize = 64
$unitHalfDiagonal = 29.5
$unitCorner = 5
$unitRim = 3

# Every drawing call below takes coordinates in output pixels. Px scales them to the 4x canvas.
function Px($v) { [single]($v * $scale) }

function New-Canvas($w, $h) {
    $bitmap = New-Object Drawing.Bitmap (Px $w), (Px $h), ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([Drawing.Color]::FromArgb(0, 5, 5, 5))
    return @{ Bitmap = $bitmap; Graphics = $g; W = $w; H = $h }
}

function Finish-Canvas($canvas) {
    $small = New-Object Drawing.Bitmap $canvas.W, $canvas.H, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gs = [Drawing.Graphics]::FromImage($small)
    $gs.CompositingMode = 'SourceCopy'
    $gs.InterpolationMode = 'HighQualityBicubic'
    $gs.PixelOffsetMode = 'HighQuality'
    $gs.DrawImage($canvas.Bitmap, 0, 0, $canvas.W, $canvas.H)
    $canvas.Graphics.Dispose(); $gs.Dispose(); $canvas.Bitmap.Dispose()
    return $small
}

# A laser module (a small block with a lens) firing a beam up and to the right that ends in a dot, drawn around
# ($cx, $cy), about 45 pixels long at $k = 1. $on draws the beam solid, off draws it dashed.
function Glyph-Laser($g, $cx, $cy, $color, [bool]$on, $k = 1) {
    $state = $g.Save()
    $g.TranslateTransform((Px $cx), (Px $cy))
    $g.RotateTransform(-35)
    $g.ScaleTransform($k, $k)
    $brush = New-Object Drawing.SolidBrush $color

    # the module: a rounded block, its lens a notch at the front
    $body = New-Object Drawing.Drawing2D.GraphicsPath
    $x = Px (-21); $y = Px (-5.5); $w = Px 13; $h = Px 11; $r = Px 3
    $body.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $body.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $body.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $body.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $body.CloseFigure()
    $g.FillPath($brush, $body)
    $g.FillRectangle($brush, (Px (-8.5)), (Px (-3)), (Px 3), (Px 6))

    $pen = New-Object Drawing.Pen $color, (Px 3)
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    if (-not $on) { $pen.DashPattern = [single[]]@(1.2, 1.6) }
    $g.DrawLine($pen, (Px (-3)), 0, (Px 13), 0)

    $g.FillEllipse($brush, (Px 14.5), (Px (-4.5)), (Px 9), (Px 9))
    $g.Restore($state)
}

# A flashlight (a body with a wider head) throwing a cone up and to the right, drawn around ($cx, $cy), about 45 pixels
# long at $k = 1. $on fills the cone, off draws its outline dashed.
function Glyph-Light($g, $cx, $cy, $color, [bool]$on, $k = 1) {
    $state = $g.Save()
    $g.TranslateTransform((Px $cx), (Px $cy))
    $g.RotateTransform(-35)
    $g.ScaleTransform($k, $k)
    $brush = New-Object Drawing.SolidBrush $color

    # the body, then the head flaring out toward the lens
    $g.FillRectangle($brush, (Px (-22)), (Px (-3.5)), (Px 11), (Px 7))
    $head = New-Object Drawing.Drawing2D.GraphicsPath
    $head.AddPolygon([Drawing.PointF[]]@(
            (New-Object Drawing.PointF (Px (-12)), (Px (-3.5))),
            (New-Object Drawing.PointF (Px (-6)), (Px (-6.5))),
            (New-Object Drawing.PointF (Px (-6)), (Px 6.5)),
            (New-Object Drawing.PointF (Px (-12)), (Px 3.5))))
    $g.FillPath($brush, $head)

    $cone = New-Object Drawing.Drawing2D.GraphicsPath
    $cone.AddPolygon([Drawing.PointF[]]@(
            (New-Object Drawing.PointF (Px (-3)), (Px (-5))),
            (New-Object Drawing.PointF (Px 22), (Px (-12))),
            (New-Object Drawing.PointF (Px 22), (Px 12)),
            (New-Object Drawing.PointF (Px (-3)), (Px 5))))
    if ($on) {
        $g.FillPath($brush, $cone)
    } else {
        $pen = New-Object Drawing.Pen $color, (Px 2.2)
        $pen.DashPattern = [single[]]@(1.4, 1.4)
        $pen.LineJoin = 'Round'
        $g.DrawPath($pen, $cone)
    }
    $g.Restore($state)
}

# Engaging with a strobe: a crosshair (a ring with four ticks across it) and four short diagonal rays around it, drawn
# around ($cx, $cy), about 46 pixels across at $k = 1. $on draws the rays solid and a dot at the center, off draws the
# rays dashed and no dot.
function Glyph-Engage($g, $cx, $cy, $color, [bool]$on, $k = 1) {
    $state = $g.Save()
    $g.TranslateTransform((Px $cx), (Px $cy))
    $g.ScaleTransform($k, $k)
    $brush = New-Object Drawing.SolidBrush $color

    $reticle = New-Object Drawing.Pen $color, (Px 3)
    $reticle.StartCap = 'Round'; $reticle.EndCap = 'Round'
    $g.DrawEllipse($reticle, (Px (-9.5)), (Px (-9.5)), (Px 19), (Px 19))
    foreach ($s in 1, -1) {
        $g.DrawLine($reticle, (Px (5 * $s)), 0, (Px (14 * $s)), 0)
        $g.DrawLine($reticle, 0, (Px (5 * $s)), 0, (Px (14 * $s)))
    }
    if ($on) {
        $g.FillEllipse($brush, (Px (-3.5)), (Px (-3.5)), (Px 7), (Px 7))
    }

    $rays = New-Object Drawing.Pen $color, (Px 3)
    $rays.StartCap = 'Round'; $rays.EndCap = 'Round'
    if (-not $on) { $rays.DashPattern = [single[]]@(1.2, 1.6) }
    for ($i = 0; $i -lt 4; $i++) {
        $a = ($i * 2 + 1) * [Math]::PI / 4
        $dx = [Math]::Cos($a); $dy = [Math]::Sin($a)
        $g.DrawLine($rays, (Px (16 * $dx)), (Px (16 * $dy)), (Px (23 * $dx)), (Px (23 * $dy)))
    }
    $g.Restore($state)
}

# A short bold label ("VIS", "IR") centered on ($cx, $cy).
function Glyph-Label($g, $cx, $cy, $color, $text) {
    $g.TextRenderingHint = 'AntiAliasGridFit'
    $font = New-Object Drawing.Font 'Segoe UI', (Px 11), ([Drawing.FontStyle]::Bold), ([Drawing.GraphicsUnit]::Pixel)
    $format = New-Object Drawing.StringFormat
    $format.Alignment = 'Center'; $format.LineAlignment = 'Center'
    $g.DrawString($text, $font, (New-Object Drawing.SolidBrush $color), (Px $cx), (Px $cy), $format)
    $font.Dispose()
}

# A per-trooper toggle in the stock toggles' style. The glyph sits above $label, or centered without one. $none draws
# it grey with the off glyph.
function Draw-UnitToggle([string]$label, [bool]$on, [bool]$hover, [bool]$none = $false, [string]$glyphKind = 'laser') {
    $canvas = New-Canvas $unitSize $unitSize
    $g = $canvas.Graphics
    $c = $unitSize / 2

    # a square with rounded corners, turned 45 degrees: its half diagonal is $unitHalfDiagonal
    $side = $unitHalfDiagonal * [Math]::Sqrt(2)
    $diamond = New-Object Drawing.Drawing2D.GraphicsPath
    $x = Px (-$side / 2); $r = Px $unitCorner; $w = Px $side
    $diamond.AddArc($x, $x, 2 * $r, 2 * $r, 180, 90)
    $diamond.AddArc($x + $w - 2 * $r, $x, 2 * $r, 2 * $r, 270, 90)
    $diamond.AddArc($x + $w - 2 * $r, $x + $w - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $diamond.AddArc($x, $x + $w - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $diamond.CloseFigure()
    $turn = New-Object Drawing.Drawing2D.Matrix
    $turn.Translate((Px $c), (Px $c))
    $turn.Rotate(45)
    $diamond.Transform($turn)

    $fill = if ($none) { $menuNone } elseif ($on) { $menuOn } else { $menuOff }
    $g.FillPath((New-Object Drawing.SolidBrush $fill), $diamond)
    if ($hover) {
        $g.DrawPath((New-Object Drawing.Pen $menuHoverRim, (Px $unitRim)), $diamond)
    }

    $glyph = if ($none) { $menuNoneGlyph } elseif ($on) { $menuOnGlyph } else { $menuOffGlyph }
    $glyphY = if ($label) { $c - 6 } else { $c }
    $k = if ($label) { 0.6 } else { 0.75 }
    switch ($glyphKind) {
        'light' { Glyph-Light $g $c $glyphY $glyph $on $k }
        'engage' { Glyph-Engage $g $c $glyphY $glyph $on $k }
        default { Glyph-Laser $g $c $glyphY $glyph $on $k }
    }
    if ($label) { Glyph-Label $g $c ($c + 12) $glyph $label }
    return Finish-Canvas $canvas
}

# Uncompressed 32-bit BGRA DDS, straight alpha, bottom-up rows (the game shows the first row at the bottom), masks
# R=0xFF0000 G=0xFF00 B=0xFF A=0xFF000000, no mipmaps.
function Save-Dds($bitmap, $path) {
    $w = $bitmap.Width; $h = $bitmap.Height
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([Text.Encoding]::ASCII.GetBytes('DDS '))
    $bw.Write([uint32]124)                  # header size
    $bw.Write([uint32]0x100F)               # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    $bw.Write([uint32]$h); $bw.Write([uint32]$w)
    $bw.Write([uint32]($w * 4))             # pitch
    $bw.Write([uint32]0); $bw.Write([uint32]0) # depth, mipmap count
    for ($i = 0; $i -lt 11; $i++) { $bw.Write([uint32]0) } # reserved
    $bw.Write([uint32]32); $bw.Write([uint32]0x41) # pixel format: size, DDPF_RGB | DDPF_ALPHAPIXELS
    $bw.Write([uint32]0); $bw.Write([uint32]32)    # fourcc, bit count
    $bw.Write([uint32]0x00FF0000); $bw.Write([uint32]0x0000FF00); $bw.Write([uint32]0x000000FF)
    $bw.Write([uint32]4278190080)           # alpha mask 0xFF000000 (a hex literal would be a negative int)
    $bw.Write([uint32]0x1000)               # caps: texture
    for ($i = 0; $i -lt 4; $i++) { $bw.Write([uint32]0) }
    for ($y = $h - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bitmap.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
        }
    }
    $bw.Flush()
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

$images = [ordered]@{
    'dk2ml_vlasers_unit_off_normal'    = Draw-UnitToggle 'VIS' $false $false
    'dk2ml_vlasers_unit_off_hover'     = Draw-UnitToggle 'VIS' $false $true
    'dk2ml_vlasers_unit_on_normal'     = Draw-UnitToggle 'VIS' $true $false
    'dk2ml_vlasers_unit_on_hover'      = Draw-UnitToggle 'VIS' $true $true
    'dk2ml_vlasers_unit_ir_off_normal' = Draw-UnitToggle 'IR' $false $false
    'dk2ml_vlasers_unit_ir_off_hover'  = Draw-UnitToggle 'IR' $false $true
    'dk2ml_vlasers_unit_ir_on_normal'  = Draw-UnitToggle 'IR' $true $false
    'dk2ml_vlasers_unit_ir_on_hover'   = Draw-UnitToggle 'IR' $true $true
    'dk2ml_vlasers_unit_none'          = Draw-UnitToggle 'VIS' $false $false $true
    'dk2ml_vlasers_unit_ir_none'       = Draw-UnitToggle 'IR' $false $false $true
    'dk2ml_vlasers_light_off_normal'   = Draw-UnitToggle '' $false $false $false 'light'
    'dk2ml_vlasers_light_off_hover'    = Draw-UnitToggle '' $false $true $false 'light'
    'dk2ml_vlasers_light_on_normal'    = Draw-UnitToggle '' $true $false $false 'light'
    'dk2ml_vlasers_light_on_hover'     = Draw-UnitToggle '' $true $true $false 'light'
    'dk2ml_vlasers_light_none'         = Draw-UnitToggle '' $false $false $true 'light'
    'dk2ml_vlasers_behave_off_normal'  = Draw-UnitToggle '' $false $false $false 'engage'
    'dk2ml_vlasers_behave_off_hover'   = Draw-UnitToggle '' $false $true $false 'engage'
    'dk2ml_vlasers_behave_on_normal'   = Draw-UnitToggle '' $true $false $false 'engage'
    'dk2ml_vlasers_behave_on_hover'    = Draw-UnitToggle '' $true $true $false 'engage'
    'dk2ml_vlasers_behave_none'        = Draw-UnitToggle '' $false $false $true 'engage'
}

New-Item -ItemType Directory -Force $outDir | Out-Null
foreach ($name in $images.Keys) {
    Save-Dds $images[$name] (Join-Path $outDir "$name.dds")
}
Write-Host "Wrote $($images.Count) textures to $outDir"

if ($PreviewDir) {
    # the images side by side at their own size, on a mid-grey backdrop
    $cell = 128
    $sheet = New-Object Drawing.Bitmap ($cell * $images.Count), $cell
    $g = [Drawing.Graphics]::FromImage($sheet)
    $g.Clear([Drawing.Color]::FromArgb(0x5a, 0x5a, 0x5a))
    $i = 0
    foreach ($name in $images.Keys) {
        $img = $images[$name]
        $g.DrawImage($img, $i * $cell + ($cell - $img.Width) / 2, ($cell - $img.Height) / 2, $img.Width, $img.Height)
        $i++
    }
    $sheet.Save((Join-Path $PreviewDir 'hud_icon_preview.png'))
    $g.Dispose()
}
