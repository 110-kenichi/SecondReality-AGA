param(
  [string]$Exe = "",
  [string]$Out = "",
  [string]$RunName = "m2.exe",
  [int]$Cylinders = 130
)
# 起動用 HDF (ハードディスク) を作成する
#
# - RDB + 単一 FFS パーティション (DH0, bootable) を rdbtool で作成
# - xdftool でフォーマット + ブートブロック設置
# - m2.exe とアセット (music1.srm / title.raw / endpic.raw / hoi.uh / fona.uh) を格納
# - S/Startup-Sequence で m2.exe を起動
#
# 使い方: .\make-hdf.ps1 -Exe ..\src\m2\out\m2.exe
$ErrorActionPreference = "Stop"
$run = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path -Parent $run
if ($Out -eq "") { $Out = "$run\m0.hdf" }
if ($Exe -eq "") { $Exe = "$proj\src\m2\out\m2.exe" }

# ツール (rdbtool / xdftool) の解決
$scripts = Join-Path $env:LOCALAPPDATA "Programs\Python\Python313\Scripts"
$rdb = Join-Path $scripts "rdbtool.exe"
$xdf = $env:AMIGA_XDFTOOL
if (-not $xdf) { $xdf = Join-Path $scripts "xdftool.exe" }
if (-not (Test-Path $rdb)) { $rdb = (Get-Command rdbtool -ErrorAction SilentlyContinue).Source }
if (-not $xdf -or -not (Test-Path $xdf)) { $xdf = (Get-Command xdftool -ErrorAction SilentlyContinue).Source }
if (-not $rdb -or -not (Test-Path $rdb)) { throw "rdbtool が見つかりません (pip install amitools)" }
if (-not $xdf -or -not (Test-Path $xdf)) { throw "xdftool が見つかりません (pip install amitools)" }
if (-not (Test-Path $Exe)) { throw "exe not found: $Exe" }

# --- RDB + パーティション ---
if (Test-Path $Out) { Remove-Item $Out -Force }
& $rdb $Out create "chs=$Cylinders,16,63" | Out-Null
& $rdb $Out init | Out-Null
& $rdb $Out add name=DH0 bootable=1 | Out-Null
& $rdb $Out change 0 dostype=0x444F5301 | Out-Null   # DOS\1 (FFS)

# --- フォーマット + ブートブロック ---
& $xdf $Out format "Work" ffs | Out-Null
& $xdf $Out boot install | Out-Null

# --- 起動スクリプト + 実行ファイル ---
$ss = Join-Path $run "boot-startup-sequence"
[IO.File]::WriteAllText($ss, "DH0:$RunName`n")
& $xdf $Out makedir S | Out-Null
& $xdf $Out write $ss "S/Startup-Sequence" | Out-Null
& $xdf $Out write $Exe $RunName | Out-Null
# データは exe と同じフォルダの data/ に置く (main.c は PROGDIR:data/ から読む)
& $xdf $Out makedir data | Out-Null
# Workbench アイコン (tools/make_icon.py で作成)
$icon = Join-Path $proj "assets\icon\m2.exe.info"
if (Test-Path $icon) { & $xdf $Out write $icon "$RunName.info" | Out-Null }

# --- アセット (DF1:/DF2: の代わりに HDF へ) ---
$music = "$proj\assets\music\converted\music1.srm"
if (Test-Path $music) { & $xdf $Out write $music ("data/" + "music1.srm") | Out-Null }
# debug: host-rendered PCM (optional; audio path test)
if ($env:M2_PCMTEST -eq "1") {
  $pcm = Join-Path $run "music1_pcm.raw"
  if (Test-Path $pcm) {
    & $xdf $Out write $pcm ("data/" + "music1_pcm.raw") | Out-Null
    $tc = Join-Path $run "pcmtest.cfg"
    [IO.File]::WriteAllText($tc, "1")
    & $xdf $Out write $tc ("data/" + "pcmtest.cfg") | Out-Null
  }
}
foreach ($img in @("title.raw", "endpic.raw", "hoi.uh", "fona.uh", "monster.bin", "monster.pal")) {
  $src = "$proj\assets\images\$img"
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $img) | Out-Null }
}

# --- WATER (RAYSCRL) アセット ---
$credits = "$proj\assets\credits"
foreach ($f in @("credits_pics.bin", "credits_font.bin")) {
  $src = Join-Path $credits $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}
$endscrl = "$proj\assets\endscrl"
if (Test-Path "$endscrl\endscrol.txt") {
  & $xdf $Out write "$endscrl\endscrol.txt" ("data/" + "endscrol.txt") | Out-Null
}
# ENDSCRL font: 25x1550, values 0..3. Taken from SecondReality++
# Parts/ENDSCRL/ENDSCRL_MAIN_Data.cpp (endscrl_font[25][1550], Unlicense).
if (Test-Path "$endscrl\endscrl_font.bin") {
  & $xdf $Out write "$endscrl\endscrl_font.bin" ("data/" + "endscrl_font.bin") | Out-Null
}
$u2e = "$proj\assets\u2e"
foreach ($f in @("u2e_pal.bin", "u2e_obj.bin", "u2e_inst.bin", "u2e_script.bin")) {
  $src = Join-Path $u2e $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}
$sinus = "$proj\assets\sinus"
foreach ($f in @("w1dta.bin", "w2dta.bin")) {
  $src = Join-Path $sinus $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}
$water = "$proj\assets\water"
foreach ($f in @("water_bg.bin", "water_pal.bin", "water_font.bin",
                 "water1.bin", "water2.bin", "water3.bin")) {
  $src = Join-Path $water $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- FOREST (MNTSCRL) アセット ---
$forest = "$proj\assets\forest"
foreach ($f in @("forest_bg.bin", "forest_pal.bin", "forest_mask.bin",
                 "forest1.bin", "forest2.bin", "forest3.bin")) {
  $src = Join-Path $forest $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- LNS&ZOOM (LENS) アセット ---
$lens = "$proj\assets\lens"
foreach ($f in @("lens_back.bin", "lens_pal.bin", "lens_ex0.bin",
                 "lens_ex1.bin", "lens_ex2.bin", "lens_ex3.bin",
                 "lens_ex4.bin", "lens_path.bin")) {
  $src = Join-Path $lens $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- JPLOGO アセット ---
$jplogo = "$proj\assets\jplogo"
foreach ($f in @("jplogo_pic.bin", "jplogo_pal.bin", "jplogo_sin.bin")) {
  $src = Join-Path $jplogo $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- DDSTARS アセット ---
$ddstars = "$proj\assets\ddstars"
if (Test-Path "$ddstars\text.raw") {
  & $xdf $Out write "$ddstars\text.raw" ("data/" + "ddstars_text.raw") | Out-Null
}

# --- PAM アセット ---
$pam = "$proj\assets\pam"
foreach ($f in @("pam.ani", "pam.pal")) {
  $src = Join-Path $pam $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- PLZPART (プラズマ) アセット ---
$plasma = "$proj\assets\plasma"
foreach ($f in @("psini.bin", "lsini4.bin", "lsini16.bin",
                 "cube_tex.bin", "cube_pal.bin", "cube_spline.bin", "cube_splcoef.bin", "cube_sin.bin")) {
  $src = Join-Path $plasma $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- TUNNELI アセット ---
$tunnel = "$proj\assets\tunnel"
foreach ($f in @("pcalc.bin", "sinit.dat")) {
  $src = Join-Path $tunnel $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- GLENZ アセット ---
$glenz = "$proj\assets\glenz"
foreach ($f in @("glenz_bg.bin", "glenz_pal.bin", "glenz_sin.bin")) {
  $src = Join-Path $glenz $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- TECHNO アセット ---
$techno = "$proj\assets\techno"
foreach ($f in @("circle0.bin", "circle123.bin", "sin1024.bin", "troll.bin", "troll.pal")) {
  $src = Join-Path $techno $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

# --- U2A (VISU) アセット ---
$u2a = "$proj\assets\u2a"
foreach ($f in @("u2a_pal.bin", "u2a_bg.bin", "u2a_obj.bin",
                 "u2a_anim.bin", "u2a_scene.bin")) {
  $src = Join-Path $u2a $f
  if (Test-Path $src) { & $xdf $Out write $src ("data/" + $f) | Out-Null }
}

Write-Output "created: $Out"
& $xdf $Out list 2>&1 | Select-Object -First 12

# --- tempo scale (100 = normal). Set $env:M2_TEMPO to change. ---
$tp = 100
if ($env:M2_TEMPO) { $tp = [int]$env:M2_TEMPO }
$tcfg = Join-Path $run "tempo.cfg"
[IO.File]::WriteAllText($tcfg, "$tp")
& $xdf $Out write $tcfg ("data/" + "tempo.cfg") | Out-Null

# --- MUSIC1 test (env M2_MUSIC1=1 で DH0:mus1.cfg を置く) ---
if ($env:M2_MUSIC1 -eq "1") {
  $m1 = Join-Path $run "mus1.cfg"
  [IO.File]::WriteAllText($m1, "1")
  & $xdf $Out write $m1 ("data/" + "mus1.cfg") | Out-Null
}

# --- start part (debug). $env:M2_START=N writes DH0:start.cfg so the demo
#     starts at part N (src/m2/parts/scenes.c index) with the music at that
#     part's position (src/m2/main.c start_pos[]). ---
if ($env:M2_START) {
  $st = Join-Path $run "start.cfg"
  [IO.File]::WriteAllText($st, "$($env:M2_START)")
  & $xdf $Out write $st ("data/" + "start.cfg") | Out-Null
}

# --- GLENZ start frame (debug). Set $env:M2_GLENZ_FRAME to skip ahead. ---
$gf = 0
if ($env:M2_GLENZ_FRAME) { $gf = [int]$env:M2_GLENZ_FRAME }
$gb = 2
if ($env:M2_GLENZ_BLEND) { $gb = [int]$env:M2_GLENZ_BLEND }
$gd = 0
if ($env:M2_GLENZ_DIAG) { $gd = [int]$env:M2_GLENZ_DIAG }
$gcfg = Join-Path $run "glenz.cfg"
[IO.File]::WriteAllText($gcfg, "$gf $gb $gd")
& $xdf $Out write $gcfg ("data/" + "glenz.cfg") | Out-Null

# --- 実機用プロファイラ ($env:M2_PROF=1 で data/prof.cfg を置く。
#     終了時に data/prof.txt へパートごとの時間を書き出す) ---
if ($env:M2_PROF -eq "1") {
  $pc = Join-Path $run "prof.cfg"
  [IO.File]::WriteAllText($pc, "1")
  & $xdf $Out write $pc ("data/" + "prof.cfg") | Out-Null
}

# --- トリプルバッファ ($env:M2_TRIPLE=1 で data/triple.cfg を置く) ---
if ($env:M2_TRIPLE -eq "1") {
  $tc = Join-Path $run "triple.cfg"
  [IO.File]::WriteAllText($tc, "1")
  & $xdf $Out write $tc ("data/" + "triple.cfg") | Out-Null
}
if ($env:M2_PREDICT -eq "1") {
  $qc = Join-Path $run "predict.cfg"
  [IO.File]::WriteAllText($qc, "1")
  & $xdf $Out write $qc ("data/" + "predict.cfg") | Out-Null
}

# --- 実機用の転送速度テスト ($env:M2_BENCH=1 で data/bench.cfg を置く。
#     起動直後に測って data/bench.txt に書き、デモは再生せずに終わる) ---
if ($env:M2_BENCH -eq "1") {
  $bc = Join-Path $run "bench.cfg"
  [IO.File]::WriteAllText($bc, "1")
  & $xdf $Out write $bc ("data/" + "bench.cfg") | Out-Null
}

# --- 画面の使われ方の集計 ($env:M2_ANALYZE=1 で data/analyze.cfg を置く。
#     ENDSCRL に入ると終了し data/analyze.txt に書く。WinUAE 用) ---
if ($env:M2_ANALYZE -eq "1") {
  $ac = Join-Path $run "analyze.cfg"
  [IO.File]::WriteAllText($ac, "1")
  & $xdf $Out write $ac ("data/" + "analyze.cfg") | Out-Null
}
