# SecondReality-AGA

1993 年の DOS メガデモ **Second Reality** (Future Crew, 開発コード名 Unreal 2) を
**Amiga 1200 + アクセラレータ (68030/68040, AGA, Fast RAM)** へ移植するプロジェクトです。

- オリジナルソース: [mtuomi/SecondReality](https://github.com/mtuomi/SecondReality) (Unlicense / パブリックドメイン)
- 移植計画・進捗・実装メモ: [`docs/ROADMAP.md`](docs/ROADMAP.md)
- 実行ファイル: [Releases](https://github.com/110-kenichi/SecondReality-AGA/releases) (v1.0.0)

## 現在の状況

| マイルストーン | 内容 | 状態 |
|---|---|---|
| M0 | 環境構築 (ツールチェーン + WinUAE/AROS で実行) | 完了 |
| M1 | フレームワーク v0 (AGA 320x256 / 8 プレーン / ダブルバッファ / VBlank / c2p) | 完了 |
| M2 | 音楽 (STMIK 互換 S3M リプレイヤ / Paula 4ch / MUSIC0・MUSIC1 再生) | 完了 |
| M3 | 最初の「動くデモ」(パイロットパート + c2p 高速化) | 完了 (stars / plasma / title / END / ALKU の 5 シーン巡回) |
| M4 | 大規模パート (全 20 シーン: ALKU / U2A / PAM / BEGLOGO / GLENZ / TUNNELI / TECHNO / PANIC / FOREST / LENS / PLZPART / DOTS / WATER / SINUS / JPLOGO / DDSTARS / END / ENDSCRL / U2E / CREDITS) | 実装済み (U2E のビル消失を修正 / GLENZ の zoomer2 を追加) |

## ディレクトリ構成

```
docs/          計画・実装メモ (ROADMAP.md)
src/m2/        デモ本体 (フレームワーク / 各パート / STMIK 互換 S3M リプレイヤ + Paula ドライバ)
tools/         PC 側ツール (Python): S3M 変換、STMIK 解析、hunk パッチ
assets/        変換済みアセット (音楽 .srm など)
run/           実行・検証用 PowerShell スクリプトと WinUAE 設定
reference/     オリジナルソース (下記手順で clone。リポジトリには含まない)
```

## 別 PC でのセットアップ

環境の準備は **AI エージェントがほぼ全て実行できます** (人間が行うのは GitHub のサインインのみ)。

| # | 実行者 | 内容 |
|---|---|---|
| 1 | AI | **ツールチェーン**: amiga-debug の VSIX をダウンロード → zip として展開 → `C:\Users\<user>\amiga-toolchain\amiga-debug-1.8.2\extension` に配置 |
| 2 | AI | **Python 3 + amitools**: `winget install Python.Python.3.13` → `pip install amitools` |
| 3 | AI | **環境変数** `AMIGA_TOOLCHAIN` / `AMIGA_WORKDIR` を設定 (下記) |
| 4 | AI | `git config --global user.name` / `user.email` |
| 5 | **人間** | **GitHub にサインイン** (push する場合) |
| 6 | AI | `git clone` (本体 + reference) |
| 7 | AI | ビルド確認: `src/m2` で `gnumake -j8` |

- VSIX の直接 URL (バージョンは marketplace で確認):
  `https://marketplace.visualstudio.com/_apis/public/gallery/publishers/BartmanAbyss/vsextensions/amiga-debug/1.8.2/vspackage`
  (約 191MB。既存 PC の `C:\Users\<user>\amiga-toolchain` をコピーして移す方法でも可 = 約 445MB)
- 環境変数:
  ```powershell
  [Environment]::SetEnvironmentVariable("AMIGA_TOOLCHAIN", "C:\Users\<user>\amiga-toolchain\amiga-debug-1.8.2\extension\bin\win32", "User")
  [Environment]::SetEnvironmentVariable("AMIGA_WORKDIR", "C:\Users\<user>\amiga-toolchain\m0run", "User")
  [Environment]::SetEnvironmentVariable("AMIGA_KICKSTART", "C:\path\to\kickstart31.rom", "User")   # KS 3.1 ROM
  ```
  (未設定の場合は `%USERPROFILE%\amiga-toolchain\...` が使われる。設定漏れは `run-m0.ps1` が明確なエラーで知らせる)
- リポジトリとオリジナルソースの取得:
  ```powershell
  git clone https://github.com/110-kenichi/SecondReality-AGA.git SecondReality-AGA
  cd SecondReality-AGA
  git clone https://github.com/mtuomi/SecondReality.git reference/SecondReality
  ```

## ビルドと実行

```powershell
# ビルド (例: src/m2)
$tool = $env:AMIGA_TOOLCHAIN
$env:PATH = "$tool\opt\bin;$tool;$env:PATH"
cd src\m2
gnumake -j8          # out/m2.exe を生成 (HUNKF_FAST パッチ適用)

# 起動用 HDF 作成 + 実行
cd ..\..\run
.\make-hdf.ps1 -Exe ..\src\m2\out\m2.exe  # m0.hdf (RDB + FFS + exe + アセット) を作成
.\run-m0.ps1                              # WinUAE 起動 (KS 3.1 + HDF)
```

- 実行には **Kickstart 3.1 ROM** が必要です。環境変数 `AMIGA_KICKSTART` に ROM のパスを設定してください
  (例: `C:\path\to\kickstart31.rom`)。ROM は著作物のためリポジトリには含めません。
  未設定時は `%USERPROFILE%\amiga-toolchain\kickstart31.rom` を使います。

## 検証用スクリプト (run/)

| スクリプト | 用途 |
|---|---|
| `run-m0.ps1` | WinUAE 起動 (KS 3.1 + `m0.hdf`。ウィンドウ移動は `AMIGA_WINUAE_MOVE=0` で無効) |
| `make-hdf.ps1 -Exe <exe>` | 起動用 HDF (DH0: FFS) を作成。exe とアセットを格納 |
| `move-winuae.ps1` | WinUAE ウィンドウを画面最下部へ (タイトルバーのみ表示) |
| `list-windows.ps1 -ProcessId <pid>` | ウィンドウ一覧 |
| `capture-hwnd.ps1 -Hwnd <hwnd> -OutPath <png> -PrintWindow` | スクリーンショット |
| `analyze-band.ps1 -Image <png> -Y0 <y0> -Y1 <y1>` | デバッグ表示バーの解析 |

## ライセンス

- 本リポジトリの移植コード: オリジナルに準じ **Unlicense** (パブリックドメイン、[`LICENSE`](LICENSE))
- オリジナル (Second Reality): Unlicense — https://github.com/mtuomi/SecondReality
- 音楽は Future Crew のリリースに準拠 (クレジット明記)

## 作業を再開するときの指示例 (AI エージェント向け)

別の PC で作業を再開する場合は、以下の指示をそのままエージェントに渡せばスムーズです。
(最新の状況は `docs/ROADMAP.md` と `git log` を参照)

> このプロジェクト (Second Reality → Amiga 1200 移植) の続きを、この PC で作業したい。
> **環境の準備もあなた (AI) がやってください。GitHub のサインインだけは私 (人間) が行います。**
>
> 【セットアップ (README.md「別 PC でのセットアップ」の表に従って実行)】
> 1. amiga-debug ツールチェーン: VSIX をダウンロード → 展開 → 所定の場所に配置
> 2. Python 3 + amitools (winget / pip)
> 3. 環境変数 `AMIGA_TOOLCHAIN` / `AMIGA_WORKDIR` を設定
> 4. git config (user.name / user.email)
> 5. GitHub サインインは人間が行う → 完了したら clone を実行
>    (本体: https://github.com/110-kenichi/SecondReality-AGA +
>     reference: https://github.com/mtuomi/SecondReality.git → reference/SecondReality)
> 6. ビルド確認 (src/m2 で gnumake -j8)
>
> 【現状 (最新コミットは git log で確認。2026-11-13 時点)】
> - M0〜M3 完了。M4 は全 20 シーン実装済み:
>   0 alku / 1 u2a / 2 pam / 3 image(BEGLOGO) / 4 glenz / 5 tunnel / 6 techno /
>   7 panic / 8 forest / 9 lens / 10 plasma / 11 dots / 12 water / 13 sinus /
>   14 jplogo / 15 ddstars(隠し) / 16 end / 17 scroll / 18 u2e / 19 credits
> - 起動は KS 3.1 実 ROM + HDF (`run/make-hdf.ps1`)。アセットも HDF に格納。
> - U2E の直近の修正 (出典付き):
>   ・投影倍率: 原作 `AVID.ASM:66-83` の `_avistan[bx]` はバイトオフセット。
>     word 添字 = bx>>1 = 56 → `avistan[56]=715` → `160*715>>8 = 446`。
>     `projmuly = 446*225>>8 = 391` (`AVIDM1.ASM:67` の projaspect=225)。
>     C++ 版 `VISU.cpp:461-476` も `avistan[bx>>1]` で一致。
>     旧値 194 は bx を word 添字と誤解したもの (2.3 倍小さかった)。
>   ・3D 近平面クリップ: 原作 `ADRAW.ASM:524-576` + `ADRAWCLP.ASM:123-290`
>     (C++ `VISU.cpp:1192-1256`) を追加。2D クリップだけでは z<512 の
>     頂点が張り付き、巨大な多角形の残像になっていた。
>     `__divdi3` をリンクしていないため `muldiv32()` を追加 (交点のみ)。
>   ・表示: 原作 320x200 を 320x256 へ 1.28 倍 (GLENZ と同じ)。
>     `projmuly=500`、ビューポート y=32..222。
>   ・終了: `0xFF 0xFF` で `u2e_done=1` (原作 `U2E.C:460-465` の `xit=1`)。
>   ・END (指輪画像): `end_finished()` を 400 フレーム固定から
>     フェードアウト完了へ (原作 `END.C:107-117`)。
>
> 【今回やること (最優先)】
> 1. **街中シーン中間の最高点でビル/地面が消える問題**
>    - カメラ y の局所最大: フレーム 908 (y=30005)。ここが「街中の最高点」。
>    - スクリプト `U2E.0AB` は 860〜900 で街のオブジェクトを OFF にしている
>      (例: 862 obj17, 902 obj27。893 で obj17 ON、917 で obj26 ON)。
>      このため on は 4〜5 個まで減る。原作どおりか要検証。
>    - 次の手順:
>      a. リファレンス実装 SecondReality++ v1.0 Windows x64 を
>         https://github.com/XorJS/SecondRealityPlusPlus/releases/tag/v1.0
>         から取得 (zip を展開)。
>      b. `SecondReality++.exe 4 W` で U2E を**ウィンドウモード**起動
>         (最大化しない)。U2E の中間最高点は起動後およそ 13 秒
>         (70fps で動く場合) 〜26 秒 (35fps の場合)。
>         `run/capture-srpp.ps1 -OutPath run\srpp-x.png` でキャプチャ。
>      c. 同じフレームを移植側で固定表示:
>         `(Get-Item src\m2\parts\u2e.c).LastWriteTime = Get-Date`
>         → `run/build-fixed.ps1 -Scene 18 -Extra "-DU2E_STOP_FRAME=908"`
>         (build-fixed.ps1 は main.c しか touch しないので、
>          u2e.c を明示的に touch しないと -D が効かない)
>      d. ホスト側の描画と比較 (当時の一時スクリプト tools/tmp_u2e_render.py は
>         公開時に削除済み)。
>    - 注意: 原作スクリプトを勝手に変えない。まず「原作でも消えるのか」を
>      確定させる。原作で消えるなら移植側のバグではない。
> 2. U2E 終了後の指輪画像 (ENDPIC) の表示期間の実機確認。
> 3. `docs/ROADMAP.md` 更新 + commit & push。
>
> 【検証の注意 (重要)】
> - 画面キャプチャは WinUAE の **子ウィンドウ (class=AmigaPowah)** を撮る:
>   `run/capture-child.ps1 -OutPath run\x.png`
>   (親ウィンドウの PrintWindow では内容が取れない)
> - WinUAE は**既定の 2 倍ウィンドウ**のまま。`gfx_width_windowed` /
>   `gfx_width` などのサイズキーを `m0.uae` に入れない
>   (この PC は Windows DPI 125% で、2.4 倍表示・クロップ・最大化風になる)。
> - 表示マッピング (実測): x0=57, sx=2.0, y0=32, sy=2.0
>   (y0 は DIW 変更後の値。以前の -16 は旧 DIW)。
> - U2E の計測バー (画面上部 y=0..31、左から):
>   objs / skip(/6) / polys(×20) / on / mulx(×16) / muly(×20) / frame(×64)
>   (x=0/24/48/112/136/160/184, 幅 20/20/60/20/20/20/12, 色254=白)
> - フレーム固定: u2e.c を touch してから
>   `run/build-fixed.ps1 -Scene 18 -Extra "-DU2E_STOP_FRAME=908"`
> - 画像は read ツールでモデルに渡さない (Python + PIL で数値解析)。
>
> 【重要な前提】
> - 応答は日本語で。権限が必要な操作は問い合わせ不要 (許可済み)。
> - 起動は KS 3.1 実 ROM + HDF (`run/make-hdf.ps1` が exe とアセットを格納)。
>   ROM: 環境変数 `AMIGA_KICKSTART` で指定 (著作物・リポジトリ外)
> - amitools (xdftool/rdbtool) が `%APPDATA%\Python\Python3xx\Scripts` にある場合は
>   PATH に追加してから `make-hdf.ps1` を実行する。
> - 検証は `run/capture-hwnd.ps1 -PrintWindow` でスクリーンショット。
> - c2p は重い → シーンはプラナー直接描画かハードウェアスクロールで。
> - 検証が終わったら必ず commit & push (別 PC と同期するため)。

### 事前に必要なもの (人間側の準備)

| 項目 | 内容 |
|---|---|
| GitHub 認証 | clone/push できる状態 (ブラウザでサインイン or Personal Access Token) |
| ツールチェーン | VS Code 拡張 `amiga-debug` をインストール (同梱の GCC/WinUAE/GDB を使用) |
| Python | Python 3 + `pip install amitools` (ADF 操作用 `xdftool`) |
| git 設定 | `git config --global user.name` / `user.email` |
