# Amiga の CF カード (既定 G:\files) へ、変わったファイルだけをコピーする。
#   コピー先は環境変数 AMIGA_CF_DIR で変えられる (既定 G:\files)。
#   ドライブが無いときは何もしない。コピー先のファイルは消さない。
#   中身は通常ビルド (最初から再生、start.cfg / prof.cfg 無し) の
#   m2.exe, m2.exe.info, data\* (run\make-hdf.ps1 と同じ構成)。
param([string]$Dest = "")
$ErrorActionPreference = "Stop"
# Get-FileHash は、pwsh 7 から呼ばれた Windows PowerShell 5 では PSModulePath の
# 違いで読み込めないことがある (2026-11-15 に実際に失敗)。.NET で直接計算する。
function FileMd5([string]$path) {
  $md5 = [System.Security.Cryptography.MD5]::Create()
  $fs = [System.IO.File]::OpenRead($path)
  try { return [System.BitConverter]::ToString($md5.ComputeHash($fs)) }
  finally { $fs.Dispose(); $md5.Dispose() }
}

$run = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($Dest -eq "") { $Dest = $env:AMIGA_CF_DIR }
if (-not $Dest) { $Dest = "G:\files" }
$root = [IO.Path]::GetPathRoot($Dest)
if (-not (Test-Path $root)) { Write-Output "deploy-cf: $root が無いのでスキップ"; exit 0 }

# 通常ビルドの HDF を一時フォルダに作って展開する
$tmp = Join-Path $env:TEMP "sr_deploy"
if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
New-Item -ItemType Directory $tmp | Out-Null
$savedStart = $env:M2_START; $savedProf = $env:M2_PROF
Remove-Item Env:M2_START -ErrorAction SilentlyContinue
Remove-Item Env:M2_PROF -ErrorAction SilentlyContinue
& "$run\make-hdf.ps1" -Out (Join-Path $tmp "sr.hdf") | Out-Null
if ($savedStart) { $env:M2_START = $savedStart }
if ($savedProf) { $env:M2_PROF = $savedProf }
$xdf = $env:AMIGA_XDFTOOL
if (-not $xdf) { $xdf = Join-Path $env:LOCALAPPDATA "Programs\Python\Python313\Scripts\xdftool.exe" }
& $xdf (Join-Path $tmp "sr.hdf") unpack $tmp | Out-Null
$src = Join-Path $tmp "Work"

$copied = 0; $same = 0
$items = @(Get-Item (Join-Path $src "m2.exe"), (Join-Path $src "m2.exe.info") -ErrorAction SilentlyContinue)
$items += Get-ChildItem (Join-Path $src "data") -File
foreach ($f in $items) {
  $rel = $f.FullName.Substring($src.Length + 1)
  if ($rel -like "data\start.cfg" -or $rel -like "data\prof.cfg") { continue }
  $d = Join-Path $Dest $rel
  $need = $true
  if (Test-Path $d) {
    $di = Get-Item $d
    if ($di.Length -eq $f.Length -and
        (FileMd5 $d) -eq (FileMd5 $f.FullName)) {
      $need = $false
    }
  }
  if ($need) {
    New-Item -ItemType Directory -Force -Path (Split-Path $d -Parent) | Out-Null
    Copy-Item $f.FullName $d -Force
    Write-Output "deploy-cf: $rel"
    $copied++
  } else { $same++ }
}
Remove-Item $tmp -Recurse -Force
Write-Output "deploy-cf: $Dest に $copied 個コピー (変更なし $same 個)"
