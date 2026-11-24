# Fabrique resources\OBSLink.ico : logo OBS + pastille « sorties multiples » (1 entrée -> 3 sorties).
Add-Type -AssemblyName System.Drawing
$root = Split-Path $PSScriptRoot -Parent
$base = [System.Drawing.Image]::FromFile("$root\resources\obs-base-256.png")

function New-Frame([int]$S) {
    $bmp = New-Object System.Drawing.Bitmap 256, 256
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.InterpolationMode = 'HighQualityBicubic'; $g.PixelOffsetMode = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.DrawImage($base, 0, 0, 256, 256)

    # pastille en bas à droite
    $cx = 178; $cy = 178; $r = 74
    $ring = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $g.FillEllipse($ring, $cx - $r - 6, $cy - $r - 6, 2 * ($r + 6), 2 * ($r + 6))
    $grad = New-Object System.Drawing.Drawing2D.LinearGradientBrush ((New-Object System.Drawing.Point 0, ($cy - $r)), (New-Object System.Drawing.Point 0, ($cy + $r)), [System.Drawing.Color]::FromArgb(255, 44, 150, 255), [System.Drawing.Color]::FromArgb(255, 20, 90, 200))
    $g.FillEllipse($grad, $cx - $r, $cy - $r, 2 * $r, 2 * $r)

    # entrée (point) + 3 sorties (flèches)
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 9
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $wb = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
    $sx = $cx - 46; $sy = $cy
    $g.FillEllipse($wb, $sx - 12, $sy - 12, 24, 24)
    $targets = @(-40, 0, 40)
    foreach ($dy in $targets) {
        $ex = $cx + 38; $ey = $cy + $dy
        $mx = $cx - 6
        $g.DrawLine($pen, $sx + 8, $sy, $mx, $ey)
        $g.DrawLine($pen, $mx, $ey, $ex - 6, $ey)
        $pts = @((New-Object System.Drawing.PointF ($ex + 12), $ey), (New-Object System.Drawing.PointF ($ex - 8), ($ey - 13)), (New-Object System.Drawing.PointF ($ex - 8), ($ey + 13)))
        $g.FillPolygon($wb, $pts)
    }
    $g.Dispose()
    if ($S -eq 256) { return $bmp }
    $out = New-Object System.Drawing.Bitmap $S, $S
    $g2 = [System.Drawing.Graphics]::FromImage($out)
    $g2.InterpolationMode = 'HighQualityBicubic'; $g2.SmoothingMode = 'HighQuality'; $g2.PixelOffsetMode = 'HighQuality'
    $g2.DrawImage($bmp, 0, 0, $S, $S); $g2.Dispose(); $bmp.Dispose()
    return $out
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = @()
foreach ($s in $sizes) {
    $f = New-Frame $s
    $ms = New-Object System.IO.MemoryStream
    $f.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += , $ms.ToArray()
    if ($s -eq 256) { $f.Save("$root\resources\OBSLink-256.png", [System.Drawing.Imaging.ImageFormat]::Png) }
    $f.Dispose()
}

# fichier .ico (entrées PNG)
$fs = [System.IO.File]::Create("$root\resources\OBSLink.ico")
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $s = $sizes[$i]; $wh = if ($s -ge 256) { 0 } else { $s }
    $bw.Write([byte]$wh); $bw.Write([byte]$wh); $bw.Write([byte]0); $bw.Write([byte]0)
    $bw.Write([uint16]1); $bw.Write([uint16]32)
    $bw.Write([uint32]$pngs[$i].Length); $bw.Write([uint32]$offset)
    $offset += $pngs[$i].Length
}
foreach ($p in $pngs) { $bw.Write($p) }
$bw.Close(); $fs.Close()
"OK $((Get-Item "$root\resources\OBSLink.ico").Length) octets"
