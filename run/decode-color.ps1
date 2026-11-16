param([string]$Image = "m2-n1.png")
Add-Type -AssemblyName System.Drawing
$run = Split-Path -Parent $MyInvocation.MyCommand.Path
$b = [System.Drawing.Bitmap]::FromFile("$run\$Image")

# 色クラス: (名前, R, G, B, 許容差)
$classes = @(
    @("ord(green)", 0, 220, 0),
    @("row(blue)", 64, 64, 255),
    @("pat(purple)", 200, 0, 200),
    @("speed(yellow)", 220, 220, 0),
    @("tempo(cyan)", 0, 200, 220),
    @("zinfo(red)", 220, 0, 0),
    @("zplus(orange)", 255, 140, 0),
    @("frame(white)", 255, 255, 255)
)

function FindRowWithColor($bmp, $cr, $cg, $cb) {
    for ($y = 35; $y -lt 580; $y++) {
        for ($x = 40; $x -lt 700; $x += 3) {
            $c = $bmp.GetPixel($x, $y)
            if ([Math]::Abs($c.R - $cr) -lt 40 -and [Math]::Abs($c.G - $cg) -lt 40 -and [Math]::Abs($c.B - $cb) -lt 40) {
                return $y
            }
        }
    }
    return -1
}

foreach ($cl in $classes) {
    $y = FindRowWithColor $b $cl[1] $cl[2] $cl[3]
    if ($y -lt 0) { Write-Output "$($cl[0]): not found"; continue }
    $ymid = $y + 6
    # ブロックのランを検出
    $runs = @()
    $inB = $false
    $st = 0
    for ($x = 10; $x -lt 730; $x++) {
        $c = $b.GetPixel($x, $ymid)
        $nb = ($c.R -gt 30 -or $c.G -gt 30 -or $c.B -gt 30)
        if ($nb -and -not $inB) { $inB = $true; $st = $x }
        elseif (-not $nb -and $inB) {
            $inB = $false
            $en = $x - 1
            if (($en - $st) -gt 10) { $runs += ,@($st, $en) }
        }
    }
    if ($inB) { $runs += ,@($st, 729) }
    $bits = ""
    $val = 0
    $i = 0
    foreach ($rr in $runs) {
        $cx = [int](($rr[0] + $rr[1]) / 2)
        $c = $b.GetPixel($cx, $ymid)
        $lit = ($c.R -gt 60 -or $c.G -gt 60 -or $c.B -gt 60)
        if ($lit) { $bits += "1"; $val = $val -bor (1 -shl $i) } else { $bits += "0" }
        $i++
    }
    Write-Output ("{0}: y={1} blocks={2} bits={3} value={4}" -f $cl[0], $y, $runs.Count, $bits, $val)
}
$b.Dispose()
