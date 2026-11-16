param([string]$Image, [int]$Y0 = 380, [int]$Y1 = 470, [int]$MinWidth = 8)
# Analyze a horizontal band of the capture: find white bars, report x-range and top y (height = Y1 - top).
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($Image)
$W = $b.Width; $H = $b.Height
$tops = @{}
for ($x = 0; $x -lt $W; $x++) {
    $top = -1
    for ($y = $Y0; $y -lt $Y1; $y++) {
        $c = $b.GetPixel($x, $y)
        if ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 180) { $top = $y; break }
    }
    $tops[$x] = $top
}
$bars = @()
$start = $null; $prev = $null
foreach ($x in ($tops.Keys | Sort-Object)) {
    $has = $tops[$x] -ge 0
    if ($has) {
        if ($null -eq $start) { $start = $x; $prev = $x } elseif ($x - $prev -le 4) { $prev = $x } else { $bars += , @($start, $prev); $start = $x; $prev = $x }
    }
}
if ($null -ne $start) { $bars += , @($start, $prev) }
Write-Output "image=$Image band=$Y0-$Y1 bars=$($bars.Count)"
foreach ($bar in $bars) {
    $minTop = 9999
    for ($x = $bar[0]; $x -le $bar[1]; $x++) { if ($tops[$x] -ge 0 -and $tops[$x] -lt $minTop) { $minTop = $tops[$x] } }
    if (($bar[1] - $bar[0] + 1) -ge $MinWidth) {
        Write-Output ("  x={0}-{1} w={2} top={3} height={4}" -f $bar[0], $bar[1], ($bar[1] - $bar[0] + 1), $minTop, ($Y1 - $minTop))
    }
}
$b.Dispose()
