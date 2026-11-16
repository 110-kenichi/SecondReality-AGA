param([string]$Image = "m2-n1.png")
Add-Type -AssemblyName System.Drawing
$run = Split-Path -Parent $MyInvocation.MyCommand.Path
$b = [System.Drawing.Bitmap]::FromFile("$run\$Image")

# ブロック行を検出: 各行の非黒ピクセル数を数え、行のランをまとめる
$rows = @()
$inRow = $false
$start = 0
for ($y = 30; $y -lt 630; $y++) {
    $cnt = 0
    for ($x = 40; $x -lt 700; $x += 4) {
        $c = $b.GetPixel($x, $y)
        if ($c.R -gt 30 -or $c.G -gt 30 -or $c.B -gt 30) { $cnt++ }
    }
    $has = $cnt -gt 3
    if ($has -and -not $inRow) { $inRow = $true; $start = $y }
    elseif (-not $has -and $inRow) {
        $inRow = $false
        $endy = $y - 1
        if (($endy - $start) -gt 8) { $rows += ,@($start, $endy) }
    }
}
if ($inRow) { $rows += ,@($start, 629) }

Write-Output "detected $($rows.Count) block rows in $Image"
$idx = 0
foreach ($r in $rows) {
    $ymid = [int](($r[0] + $r[1]) / 2)
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
    Write-Output ("row {0}: y={1}-{2} blocks={3} bits={4} value={5}" -f $idx, $r[0], $r[1], $runs.Count, $bits, $val)
    $idx++
}
$b.Dispose()
