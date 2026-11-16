# M0: WinUAE で m0.exe を実行するスクリプト
param([int]$Start = -1, [string]$Config = "m0.uae")
# -Start N: そのパート (scenes.c の番号) から、対応する音楽位置で始める。
#   例: run/run-m0.ps1 -Start 12   (RAYSCRL)
if ($Start -ge 0) { $env:M2_START = "$Start" } else { Remove-Item Env:M2_START -ErrorAction SilentlyContinue }
$ErrorActionPreference = "Stop"
$run = Split-Path -Parent $MyInvocation.MyCommand.Path
$tool = $env:AMIGA_TOOLCHAIN
if (-not $tool) { $tool = "$env:USERPROFILE\amiga-toolchain\amiga-debug-1.8.2\extension\bin\win32" }
if (-not (Test-Path "$tool\winuae-gdb.exe")) {
  throw "WinUAE が見つかりません: $tool\winuae-gdb.exe`n環境変数 AMIGA_TOOLCHAIN を設定してください (README.md「別 PC でのセットアップ」参照)"
}
$ascii = $env:AMIGA_WORKDIR
if (-not $ascii) { $ascii = "$env:USERPROFILE\amiga-toolchain\m0run" }

# ASCII パスに媒体をコピー(日本語パス対策)し、設定を WinUAE ディレクトリへ配置
New-Item -ItemType Directory -Force -Path $ascii | Out-Null
# m0.hdf は必ず m2.exe から作り直す (古いビルドを実行してしまう事故を防ぐ)
$repo = Split-Path -Parent $run
$exe = Join-Path $repo "src\m2\out\m2.exe"
if (Test-Path $exe) {
  $ei = Get-Item $exe
  Write-Output ("rebuilding m0.hdf from m2.exe ({0}, {1} bytes)" -f $ei.LastWriteTime.ToString("HH:mm:ss"), $ei.Length)
  & "$run\make-hdf.ps1" -Exe $exe | Out-Null
}
if (-not (Test-Path "$run\m0.hdf")) {
  throw "起動用 HDF がありません: $run\m0.hdf`nrun\make-hdf.ps1 で作成してください"
}
Copy-Item "$run\m0.hdf" "$ascii\m0.hdf" -Force

# m0.uae の @AMIGA_WORKDIR@ / @AMIGA_KICKSTART@ を実際のパスに置換して WinUAE へ配置
# (WinUAE の設定ファイルは環境変数や相対パスを展開しないため)
$cfg = Get-Content (Join-Path $run $Config) -Raw
# Kickstart ROM (KS 3.1) のパス: 環境変数 AMIGA_KICKSTART で指定する
$ks = $env:AMIGA_KICKSTART
if (-not $ks) { $ks = "$env:USERPROFILE\amiga-toolchain\kickstart31.rom" }
if (-not (Test-Path $ks)) {
  throw "Kickstart 3.1 ROM が見つかりません: $ks`n環境変数 AMIGA_KICKSTART を設定してください"
}
$cfg = $cfg.Replace("@AMIGA_WORKDIR@", $ascii).Replace("@AMIGA_KICKSTART@", $ks)
[IO.File]::WriteAllText("$tool\default.uae", $cfg)

Start-Process -FilePath "$tool\winuae-gdb.exe" -ArgumentList @("-portable") -WorkingDirectory $tool
Write-Output "WinUAE started (KS 3.1 + m0.hdf)"

# ウィンドウを画面下部へ移動する (環境変数 AMIGA_WINUAE_MOVE=0 で無効化)
# 画面下部へ寄せると PrintWindow でキャプチャしやすいが、通常のデスクトップで
# 作業する場合は 0 にしてウィンドウをそのままの位置に残す。
if ($env:AMIGA_WINUAE_MOVE -ne "0") {
  powershell -NoProfile -ExecutionPolicy Bypass -File "$run\move-winuae.ps1" -WaitSeconds 90 -Mode bottom | Out-Null
} else {
  Write-Output "WinUAE window left in place (AMIGA_WINUAE_MOVE=0)"
}
