# tools — PC 側 調査・変換ツール

Second Reality の資産を調査・変換するための PC 側ツール置き場。
(Amiga 側では実行しない)

## s3m_info.py

S3M のヘッダとチャンネル使用状況を表示する解析スクリプト。

```powershell
python s3m_info.py <file.s3m> [<file2.s3m> ...]
```

実測結果 (Second Reality の音楽):

| ファイル | 曲名 | orders | instruments | patterns | 使用 ch |
|---|---|---|---|---|---|
| MUSIC0.S3M | UnreaL ][ - The 2ND Reality | 80 | 44 | 77 | 8ch (0〜7) |
| MUSIC1.S3M | UnreaL ][ / PM | 100 | 54 | 77 | 8ch (0〜7) |

※ 1 モジュール内の複数オーダーをパートごとに再生する使い方 (STMIK の
`zgotosong(order,row)` 相当) のため、曲数よりオーダー構成が重要。

## reference_s3m_load.c

libxmp の S3M ローダ (`src/loaders/s3m_load.c`) の参照用コピー。
パターン展開仕様 (bit5=note+ins, bit6=vol, bit7=cmd+info, 下位5bit=ch) の
確認に使用。ライセンスはファイル先頭の MIT 条項に従う。

# assets/music — 音楽データ

| ファイル | 内容 |
|---|---|
| `mod_2nd_skav.s3m` | 「UnreaL ][ - The 2ND Reality」標準 S3M 版 (ModArchive moduleid 212082) |
| `mod_2nd_pm.s3m` | 「UnreaL ][ / PM」標準 S3M 版 (ModArchive moduleid 212083) |

**経緯**: リポジトリ同梱の `SecondReality/MAIN/MUSIC0.S3M` / `MUSIC1.S3M` は、
ヘッダ・インストゥルメント・サンプル波形が標準版とバイト単位で一致する一方、
**パターンデータ領域のみが標準 ST3 パッキングと異なる符号化**になっている
(行終端 0x00 がほぼ無く高エントロピー)。ModArchive の標準形式版は同構造・
同サイズで正常に解釈できるため、移植では標準版を入力として使用する。

---

# その他のツール (M0 で使用)

## amitools (pip, Windows 動作)

Amiga のディスクイメージ/ファイルシステム操作用ツール群。

- インストール: `python -m pip install --user amitools machine68k greenlet`
  - 注意: PyPI の amitools 0.8.1 は machine68k 0.5.0 と非互換。
    開発版 `pip install --user git+https://github.com/cnvogelg/amitools.git` を使用する
- `xdftool` … ADF/HDF の読み書き (`list`, `format`, `write`, `delete`, `read`)
- `rdbtool` … RDB パーティション作成 (`create size=32M` → `init` → `add name=DH0 dostype=ffs pri=0 bootable=1`)
- `romtool` / `hunktool` … ROM / hunk ファイル解析

用途:

- 起動用 FFS HDF (`run/m0.hdf`) の作成 (`run/make-hdf.ps1`)

## 注意 (WinUAE 運用)

- エミュレータに渡す媒体 (HDF) は **ASCII のみのパス**に置く
  (日本語パスは WinUAE が読めない)。`run/run-m0.ps1` が
  環境変数 `AMIGA_WORKDIR` (既定 `%USERPROFILE%\amiga-toolchain\m0run`) へ自動コピーする
- WinUAE は `-portable` + `bin\win32\default.uae` の自動読込で起動する
  (`-f` は GUI を開くだけで自動実行されない)
