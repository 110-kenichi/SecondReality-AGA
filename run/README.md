# run — 実行・検証環境 (WinUAE + KS 3.1 + HDF)

## 構成

| 要素 | 内容 |
|---|---|
| エミュレータ | `vscode-amiga-debug` 同梱の改造版 WinUAE (`winuae-gdb.exe`) |
| ROM | Kickstart 3.1 ROM (著作物のためリポジトリには含めない。各自で用意し `AMIGA_KICKSTART` に設定) |
| 起動媒体 | `make-hdf.ps1` が作る `m0.hdf` (RDB + FFS、`S/Startup-Sequence` が m2.exe を起動) |

## 環境変数

| 変数 | 内容 | 既定値 |
|---|---|---|
| `AMIGA_TOOLCHAIN` | amiga-debug の `extension\bin\win32` | `%USERPROFILE%\amiga-toolchain\amiga-debug-1.8.2\extension\bin\win32` |
| `AMIGA_WORKDIR` | WinUAE に渡す媒体を置く ASCII パス | `%USERPROFILE%\amiga-toolchain\m0run` |
| `AMIGA_KICKSTART` | Kickstart 3.1 ROM (512KB) | `%USERPROFILE%\amiga-toolchain\kickstart31.rom` |
| `AMIGA_CF_DIR` | 実機用 CF カードのコピー先 (`deploy-cf.ps1`) | `G:\files` |

`m0.uae` / `m0-68020.uae` の `@AMIGA_KICKSTART@` と `@AMIGA_WORKDIR@` は
`run-m0.ps1` が上の値に置き換えてから WinUAE に渡す (WinUAE の設定ファイルは
相対パスや環境変数を展開しないため)。

## 実行手順

```powershell
# 1. ビルド
cd ..\src\m2
gnumake -j8

# 2. 実行 (m0.hdf は毎回 m2.exe から作り直す)
cd ..\..\run
.\run-m0.ps1            # 最初から
.\run-m0.ps1 -Start 9   # 途中のパートから (main.c start_pos[])
```

## 重要な設定ポイント (ハマりどころ)

- `use_gui=no` が無いと Properties GUI が開き、エミュレーションが自動開始しない
- `-f <config>` は GUI を開くだけ。**`-portable` + `default.uae` 自動読込**が正解
- **日本語パスを含む媒体は読めない** (HDF は ASCII パス `AMIGA_WORKDIR` に
  コピーしてから使う)
- **`ntsc=false` に加え、プログラム側も `BEAMCON0 = 0x0020` (PAL) を書く**
  (AGENTS.md「ビデオモード: NTSC 禁止」)
- WinUAE のエミュレートマウスは合成マウスイベント (`mouse_event`) を受け付けないため、
  マウス入力の自動テストは不可 (実機または手動操作で確認)

## ファイル

| パス | 内容 |
|---|---|
| `m0.uae` | WinUAE 設定 (A1200, 68030, 2MB Chip + 8MB Fast, PAL, HDF 起動) |
| `m0-68020.uae` | 同 68020 版 |
| `make-hdf.ps1` | `-Exe` の exe とアセットから起動用 `m0.hdf` を作成 |
| `run-m0.ps1` | HDF 作成 → default.uae 配置 → WinUAE 起動 |
| `deploy-cf.ps1` | 実機用 CF カードへ変わったファイルだけをコピー |
| `capture-*.ps1`, `list-windows.ps1`, `press-key.ps1`, `close-window.ps1` | 自動検証用ヘルパ |
