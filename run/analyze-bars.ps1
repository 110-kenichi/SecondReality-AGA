param([string]$Image)
# Analyze the planar debug display: find white bars and report their x-range and height.
Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile($Image)
$W = $b.Width; $H = $b.Height
# find white pixels in the content area (skip title bar)
$cols = @{}
for ($y = 50; $y -lt $H; $y++) {
    for ($x = 0; $x -lt $W; $x++) {
        $c = $b.GetPixel($x, $y)
        if ($c.R -gt 180 -and $c.G -gt 180 -and $c.B -gt 180) {
            if (-not $cols.ContainsKey($x)) { $cols[$x] = @() }
            $cols[$x] += $y
        }
    }
}
# group contiguous x columns into bars
$xs = $cols.Keys | Sort-Object
$bars = @()
$start = $null; $prev = $null
foreach ($x in $xs) {
    if ($null -eq $start) { $start = $x; $prev = $x }
    elseif ($x - $prev -gt 3) {
        $bars += , @($start, $prev)
        $start = $x; $prev = $x
    } else { $prev = $x }
}
if ($null -ne $start) { $bars += , @($start, $prev) }
Write-Output "image=$Image size=${W}x${H} bars=$($bars.Count)"
foreach ($bar in $bars) {
    $ys = @()
    for ($x = $bar[0]; $x -le $bar[1]; $x++) { $ys += $cols[$x] }
    $miny = ($ys | Measure-Object -Minimum).Minimum
    $maxy = ($ys | Measure-Object -Maximum).Maximum
    Write-Output ("  x={0}-{1} width={2} y={3}-{4} height={5}" -f $bar[0], $bar[1], ($bar[1] - $bar[0] + 1), $miny, $maxy, ($maxy - $miny + 1))
}
$b.Dispose()
