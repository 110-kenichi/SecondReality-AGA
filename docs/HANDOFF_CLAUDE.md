# Claude Code 引き継ぎプロンプト — SecondReality-A1200

以下をそのまま Claude Code の最初のプロンプトとして使える。

---

## 最新の状態 (2026-11-15、この節がそれより下の古い記述より優先)

### 2026-11-16 昼の追加 (この小節が最優先)

- `594f51f` CRED: 絵は横方向のハードウェアスクロール、文字は Copper による
  画面分割 (表示 128 行目で切り替え、128・129 行は黒)。実機で OK (ユーザー確認)。
  なお 594f51f には公開準備の削除 (run/dh0、src/m0・m1・mt、AROS の ADF、
  make-adf 系、ref_2nd_reality.s3m、tools/tmp_*.py) も入っている。
- `3e5664e` TECHNO TROLL: ハードウェアスクロール化。実機で OK。
- DOIT の最後のスライドアウトはハードウェアスクロール化しないと決定。
- **公開済み**: https://github.com/110-kenichi/SecondReality-AGA (public)。
  `853f868` (履歴なしの初回コミット) + `f1d83bb` (dist readme)。
  リリース v1.0.0 (タグ → f1d83bb、SecondReality-AGA-v1.0.0.zip。HDF は
  AmigaOS のファイルを含むおそれがあるので添付しない)。
- **以後の作業は新リポジトリ**: remote `origin` = SecondReality-AGA、作業ブランチ
  `claude/project-thread-3ss2xa`、`main` はリリース用。旧リポジトリ
  SecondReality-A1200 は勤務先メール等が履歴に残るため private のアーカイブとして
  remote `old` に残す (手元のブランチ `old-main` / `old-claude-project-thread-3ss2xa`)。
  **旧履歴 (853f868 を祖先に持たないコミット) を origin に push しない。**
  `.git/hooks/pre-push` が SecondReality-AGA への push でこれを拒否する
  (フックは git 管理外。別 PC で clone したら旧履歴が無いので不要)。
  WinUAE 設定 (run/*.uae) の ROM・HDF パスは `@AMIGA_KICKSTART@` /
  `@AMIGA_WORKDIR@` で、run-m0.ps1 が環境変数 (既定 %USERPROFILE%\amiga-toolchain)
  から埋める。

### 2026-11-16 未明の追加

- `6cb1790`・`3e5664e` TECHNO TROLL: スライドインとリップルをハードウェア
  スクロール (幅 704 のチップ画面 [黒 320|絵 320|黒 64]、表示開始 x = 320 - pan)
  で動かす。毎フレームの描画・c2p は無し。techno_release でチップ RAM を返す。
  BPLCON1 の細かい横スクロールを実際に使った初めてのパート。実機で OK (ユーザー確認)。
- `fac4418` 3DSINFLD が黒画面のまま (BGM は進む): 原因はファスト RAM 不足
  (prof mark SINUS-nomem)。ALKU・MNTSCRL・RAYSCRL の release でファスト RAM も
  返し、sinus は確保できるまで毎フレーム再試行。WinUAE -Start 6 とユーザー確認で OK。
- `3e92953` prof の mark: SINUS-wait/start/nomem/nodata/notready と EXIT
  (終了時の音楽位置)。WinUAE では PowerShell の mouse_event で左クリックして
  終了させ、HDF から data/prof.txt を読んだ。
- `c53ae57` GLENZ: ワイプで消す 2 行を画面バッファでも消す (MAIN.C:555-556)。
  赤い物体の直前に下部へ赤い矩形が一瞬出ていた。
- `5aa9290` MINVBALL: 色番号を 0..63 に付け替えて 6 プレーン描画・転送。
  `9b65f57` TUNNELI: 原作 TUN10.PAS:121-125 の 6bit パレット (128+x 番の切り捨て、
  64 番は黒、128 番は 2 本目のランプ) にし 6 プレーン (ユーザー承認)。
  `3ca56ab` 両パートを原作の 320x200 座標で 28..227 行に描き、6 プレーン x 200 行を
  転送 (ユーザー指示)。
- `01c67fe` MINVBALL の影の色を原作 ASM.ASM:113 の色 87 (床ランプ a=23) に合わせた
- 他エージェントの変更: `486b2b8`・`41d85e6` TECHNO の 4/3 プレーン化、`d4ea1d2`
  GLENZ zoomer2 の範囲、c2p_merge.asm に 4 プレーン版 (gen_c2p_asm.py から生成)。

### 2026-11-15 夕方の追加 (この小節が最優先、HEAD fdb53e8 以降)

**c2p (画面転送) の高速化と範囲の絞り込み**。いずれも PC で「旧版と chunky が
毎フレーム一致」「裏画面が 1 ドットずつの基準 c2p と毎フレーム一致」を
確認してからコミットした (検証用ハーネスはスクラッチに作った使い捨て。
作り方: パートの .c をホスト gcc でコンパイルし、旧版は objcopy
--redefine-sym で o_ 接頭辞を付けて同時にリンクして比べる)。

- 実機 bench (68060、64000 バイトあたり走査線): チップ書き込み 32bit 225 /
  16bit 449 / 8bit 900、チップ読み 254、ファスト書き 40、C 版 c2p 297、
  アセンブラ c2p 275、Blitter コピー 414・合成 828 (CPU より遅いので不採用)、
  div32 1 回 約 0.8us、muldi3 1 回 約 2.5us。メモリ hw-bench-060.md にも記録。
- `007511a` アセンブラ c2p (`framework/c2p_merge.asm`、`tools/gen_c2p_asm.py`
  で生成。手で編集しない)。320x256 (プレーン間隔 10240) 専用、他は C 版。
  bench の `c2p_asm_check ok` で実機でも出力一致を確認済み。
- `c2p8_cols` (32 ドット列単位)、`c2p_track` (書いた矩形を記録し、裏画面ごとに
  前回からの分だけ変換。重なる矩形はまとめる。`c2p_track_flush_rows` は
  変換した行範囲を返す) を `framework/c2p.c` に追加。
- GLENZ の zoomer2 (`GLENZ/ZOOMER.C:18-81`) は、初回の各裏画面を全体変換し、
  以後は上下の黒帯が新たに覆った行だけを c2p する。閉じ切った後の全画面クリアは
  全体変換。描画内容・タイミングは変更していない。ホストで48フレームを検証し、
  全画面描画から得た chunky の全面 c2p と、交互の裏画面への部分 c2p が毎フレーム一致。
  実機 prof は未取得。
- パートごとの方針 (ユーザー決定):
  - JPLOGO `438ef4d` 絵のある列だけ / CRED `23fb69f`・`fdb53e8` 絵と文字の矩形だけ、
    止まっている間は描画も変換もしない / U2A `269aacd`・`251927c` ポリゴンの外枠だけ
    背景復元と変換 / LNS&ZOOM `251927c` 書いた範囲だけ (回転ズームは全体) /
    GLENZ `dee8a83` 本編は復元枠+描画枠だけ / PANIC `cbd24c5` 絵のある行だけ /
    3DSINFLD `70593ae` 地形の行だけ / MNTSCRL `357f047`・RAYSCRL `fdec839`
    位置リストの点がある列だけ + 1 点ごとの割り算を除去 /
    TECHNO `dd3bae6` 白フラッシュ・ワイプの場面は変わった矩形と行だけ。
  - **U2E と PLZPART は全画面のまま** (ユーザー指示)。200 行のパートは
    c2p_scene が 200 行で変換している。TUNNELI・MINVBALL は 256 行を使う作り。
- `ccac3fb` PAM: OUT.ANI の差分は 2 フレーム前との差 (ANIM.C:28-32、
  OUTTAA.C:51-60 は 2 ページ交互)。2 枚のバッファに交互に展開するよう修正
  (以前は爆発の一部が欠けて見えた)。爆発の見た目は2026-11-16にユーザー確認済み。
- `e4a574c` 集計モード: `data/analyze.cfg` (make-hdf は `M2_ANALYZE=1`) で、
  パートごとの「0 でない列・行」「変わるブロックの割合」を
  `data/analyze.txt` に書く。`-Start N` と併用するとそのパートの終わりで終了。
  WinUAE 用。
- 実機 prof (この夕方の版、frame 平均、前 → 後): U2A 1003→588、GLENZ 713→571、
  PANIC 631→320、MNTSCRL 929→595、LNS&ZOOM 627→464、RAYSCRL 890→429、
  3DSINFLD 1225→898、JPLOGO 629→580、CRED 626→368。TECHNO 584 は白フラッシュ
  修正 (dd3bae6) が入る前の値の可能性あり。U2E 1245・PLZPART 878・
  TUNNELI 1139・MINVBALL 1125 は未対策。
- **未完了・次の候補**:
  - TECHNO 白フラッシュと CRED の修正 (fdb53e8) 後の実機 prof・見た目の確認。
  - GLENZ: 2つ目の赤いオブジェクト出現時、画面下部に一瞬赤い矩形ゴミが見える。
    原因未特定。ユーザー指示で調査を一時停止 (2026-11-16)。
  - 最新実機 prof (2026-11-16): TECHNO 1587 frames, render 236/1040,
    c2p 139, frame 559/1378。CRED 5253 frames, render 66/438, c2p 0,
    frame 323/1662。見た目の確認は未完了。
  - PH_TRANS は承認により c2p と shadow→Chip 転送を下位4プレーンに限定する試験を実装。
    `c2p4` の全画面・部分列変換は8プレーン基準出力との一致を起動時自己テストで確認する。
    `gnumake -j8` 成功。実機 68060 の prof と表示確認は未実施。
  - DOIT は4bit値のpopcount (0..4) を3bit値へ詰め、パレットを原作と同じ5色へ再割当て。
    既存の各入力プレーンのライン補正後に変換し、shadow→Chip転送を3プレーンへ限定。
    ビルド成功、全16入力値と16段階のパレット色一致を検証。実機prof・表示確認は未実施。
  - 3DSINFLD は render 688 (c2p 込み)。地形の ray march 自体が重い。
  - MINVBALL は集計が 146 フレームしか取れず未判断 (玉と影のある行だけ写す案)。
  - JPLOGO は render 308 (c2p 込み) で frame 580 (約 2 VBL)。
  - TUNNELI は commit (330) が全体。点が全行にあるので絞れない見込み。

### 2026-11-15 後半の追加 (この小節が最優先)

- `943154e` deploy-cf: MD5 を .NET で計算 (pwsh 7 から呼んだ PowerShell 5 で
  `Get-FileHash` が読めず失敗していた)。
- `e7bb1e0` / `93f921a` JPLOGO: 1 ドットごとの割り算・分岐・全面消去を除去。
  旧版と新版を PC でコンパイルし 1200 VBL 分の画面を比較して差 0。
  実機 prof で render 平均 498 -> 130 走査線。frame は c2p (約 310) が
  支配的で約 2 VBL のまま (c2p を削る根拠は無し、画面全体が毎フレーム変わる)。
- `3876b25` prof: VERTB の処理が遅れたとき走査線時計が 1 VBL 戻る問題を修正
  (PLZPART の render 最大が 4294967250 と出ていた)。
- `548efb8` / `8f65102` 実機用の速度テスト: `data/bench.cfg` を置いて起動すると
  `data/bench.txt` に、チップ/ファスト RAM の読み書き (32/16/8bit)、c2p、
  Blitter (塗り・コピー・合成・BLITHOG)、Blitter と CPU の取り合い、
  順次/ランダム読み、mul32/div32/mul64 の時間を書いて終了する。
  WinUAE で動作確認済み、**実機の結果待ち**。結果を見てからアセンブラ c2p や
  Blitter 併用の可否を判断する (推測で作らない)。
- `fa65589` パートのソースから `test_` を外した (`parts/test_x.c` -> `parts/x.c`)。
  リンク順が変わったため m2.exe はバイト単位では以前と一致しない。
- ユーザーへの返信は日本語で書く。


- ブランチ `claude/project-thread-3ss2xa`、この引き継ぎ開始時の HEAD は
  `9e49d0d` (push 済み)。
  `gnumake -j8` は最新ソースで成功 (再ビルド不要)。`run/deploy-cf.ps1 -Dest
  G:\files` は 70 個すべて変更なしと確認し、CF の `m2.exe` と
  `src/m2/out/m2.exe` の MD5 も一致。前回報告された `Get-FileHash` エラーは
  現環境では再現せず、CF 配布を確認済み。
- 目標は **実機 (ユーザーの 68060 50MHz、68030 も考慮) で動くこと**。
  68020 は対象外。必ず `AGENTS.md` の「性能測定」「音楽位置の待ち」
  「ビルド・検証・配布」を読む。

### 2026-11-14〜15 に入れた主なもの
- 原作再現: CRED の絵/文字を最終版 (C++ 版データ) に、MNTSCRL を READ2.PAS
  (最終版) どおりに、GLENZ の床クリップ (ccliplist)、TROLL の palfade、
  JPLOGO の色 0→64、U2E の fadeset (Mode X バイト単位)、プラズマの pompota、
  Rotazoom 白フラッシュ (MUSIC1 44:11)、MNTSCRL 文字フェード (35:17)。
- 音楽: 曲の終わりで止める (ループしない)、E00/F00 の記憶、ピリオドを
  c2spd 込みの ST3 単位に (ポルタメントが上がり切らない問題)。
- GLENZ は 320x200 を中央に表示 (引き伸ばし廃止、ユーザー指示)。
- 実機の速度: `-m68020-60`、FMODE=3、マージ方式 c2p (`framework/c2p.c`)、
  200 行だけの c2p と帯の消去 (`main.c c2p_scene`)、TECHNO の 1bit ページ
  直接描画、ファスト RAM の写し (`video_back_shadow`) など。
- U2A/U2E のポリゴン描画: U2A は近平面より手前にない頂点の投影を
  オブジェクト単位のキャッシュから再利用。U2E は全頂点が表示領域内の
  ポリゴンで画面クリップを省略し、単色スパンは `memset` で塗る。
  ピクセル結果を維持する演算整理で、実機の性能差は未計測。
- U2A/U2E のポリゴン塗りを SecondReality++ VISU の画素中心付き 16.16 辺走査と
  右端排他スパンへ統一。U2E は辺の傾きを一度だけ除算で求め、行ごとの交点計算を
  加算化。色付きスパンは C++ の 8.8 辺色とBresenham補間に合わせた。画素境界が
  変わるため、最新profは取得済みだが、実機表示の画素比較は未記録。最新profの
  U2A は 166 frames, render avg/max
  529/1021, c2p avg 310, frame avg/max 1106/1757。U2E は 662 frames,
  689/1583, c2p 310, frame 1251/2291。
- PLZPART/MINVBALL: PLZPART は行ごとの中間ラインコピーをなくして画面へ
  直接描画。MINVBALL はダブルバッファごとに前フレームのボール/影の位置を
  背景色へ戻し、3x3 ボールと2画素の影のプレーン更新を同一バイト単位に集約。
  原作 DOTS/ASM.ASM の旧位置復元と PLZPART/PLZ.C + ASMYT.ASM の描画式は維持。
  最新profは PLZPART 1169 frames, render avg/max 378/756, c2p 309,
  frame 888/1590。MINVBALL は 514 frames, 544/734, c2p 330,
  frame 1125/1429。変更前の比較値は未取得。
- PLZPART キューブ: 描画領域を標準表示の200行 (y=28..227) に限定。
  投影倍率を VECT.C:198-199 の x=256/640幅、y=142 に戻し、Amiga の320幅では
  x倍率を128に換算。頂点±125とスプラインの移動・進行は維持。ビルド済みで、
  最新profのPLZPART値は取得済み。画面表示の比較結果は未記録。
- TECHNO 開始時の音切れ対策: `techno_init()` の固定素材変換・パック・テーブル
  生成を `techno_prepare()` に分離し、全データロード後かつ `TakeSystem()` /
  音楽初期化前に実行。開始時は準備済みを確認してタイムライン状態だけを初期化。
  ビルド済み、実機での音声確認は未実施。
- TECHNO の白フラッシュ: 原作 `KOE.C` の palette-only 4段階フラッシュに合わせ、
  画素が変わらない区間は既表示の干渉円フレームを保持し、chunky 再構築・c2p・
  chip RAM 転送を省略。最新prof値は取得済みだが、変更前との性能比較と
  白フラッシュの画面確認は未記録。
- 実機の安定性: 音楽の窓の待ちを「到達または通過」に (各パート)、
  メモリ解放、WBStartup、ENDSCRL のチップ RAM 削減。
- プロファイラ (`data/prof.cfg` → `data/prof.txt`)。
- 配布: `PROGDIR:data/`、アイコン、`docs/dist/LICENSE.txt`・`readme.txt`、
  テンプレート由来コードを自前 (`support/crt.c`) に置き換え。
- 最新実機profの主な他の値: TECHNO render 242/1124, c2p 161,
  frame 586/1923 (1515 frames); TUNNELI 640/811, c2p 329,
  frame 1133/2287 (218 frames); MNTSCRL render 737/805;
  RAYSCRL 693/921; 3DSINFLD 571/598, c2p 310; CRED 114/155,
  c2p 321, frame 626/1742 (2730 frames). 値の単位は走査線 (1 line = 64us)。

### 残っていること
- 最新profでは MNTSCRL (render avg 737), RAYSCRL (693), U2E (689),
  TUNNELI (640), 3DSINFLD (571) が調査候補。平均値は一回の実測なので、
  これだけを根拠に最適化やブリッター変更をせず、まず同一条件で再測定する。
- U2E は以前の 628 frames, render 773/1804, c2p 310, frame 1319/2561 に対し、
  最新は 662 frames, 689/1583, c2p 310, frame 1251/2291。別実行のため
  厳密なA/Bではないが、render平均は約11%、最大は約12%低い。U2Aの変更前値は未取得。
- PLZPART/MINVBALL は変更後の実機値を取得済みだが、変更前との比較値は未取得。
- 音楽が原作動画より約 0.7〜0.8% 遅い (仮説: 原作 SB の再生レートの丸め、
  U2.ASM:737-743)。変えるかはユーザー判断待ち。
- 実機での確認待ち: 2 回目の起動、ENDSCRL の開始、PANICEND の帯、
  MNTSCRL の開始/終了、Rotazoom/PLZPART の進行。

## 0. 最初に読むもの

- 作業ディレクトリ: このリポジトリのルート
- **必ず `AGENTS.md` を読む。** 特に:
  - 想像で実装しない。値の根拠は原作 (`reference/SecondReality/...`) の
    ファイル名と行番号、または C++ 版
    (`XorJS/SecondRealityPlusPlus` の `Parts/...cpp`) の行番号、
    またはこのビルドで実測した数値のどれか。
  - 70Hz → PAL 50Hz の換算は式をコメントに書く。
  - PAL 50Hz 固定。`BEAMCON0=0x0020` を維持する。NTSC 禁止。
  - 画像は `read` でモデルに渡さず、Python + PIL でピクセル解析する。
  - 検証が終わったら commit & push。
- この節を作成した時点の HEAD は `fb633ea` (branch
  `claude/project-thread-3ss2xa`, origin と一致)。ソース変更は `b516283` と
  `fb633ea` にコミット済み。
- `gnumake -j8` は最新ソースで成功し、再ビルド不要。標準CF配布を再実行し、
  70個すべて変更なし、CFとローカルの `m2.exe` のMD5一致を確認済み。
- このハンドオフ更新以外の作業ツリー変更はない。
- `run/` の検証キャプチャ (PNG/CSV) と `run/m0.hdf` は整理のため削除。
  `run-m0.ps1` が HDF を自動で作り直す。

## 1. 過去セッションでやったこと (当時のコミット記録)

以下は古い時点の履歴で、HEADや未完了項目は上の「最新の状態」を参照:

### 1.1 ENDSCRL の「Copyright (C)」修正 (scroll.c)

- 原作 `reference/SecondReality/ENDSCRL/MAIN.C:13` の `fonaorder` は
  `':'` と `'('` の間に **0x8f が 2 文字**ある:
  `"...0123456789!?,.:\x8f\x8f()+-*='"`
- フォントシート `FONA.UH` にもその 2 グリフが存在する。
- 移植版はこの 2 文字を飛ばしていたため、`(` 以降が 2 グリフずれ、
  `Copyright (C)` が `Copyright 'C'` になっていた。
- 修正: `scan_font()` の `o` 文字列に `\x8f\x8f` を追加。
- 検証済み: 該当行を PIL で 184x40 に縮小し、`(C)` が正しい括弧
  グリフで描かれていることを確認 (キャプチャは整理のため削除済み)。

### 1.2 PLZPART 終端の下方向スライドアウト + ハードウェア縦シフト

**ユーザーの指摘:**
- 「Plazma の最後のモノトーンのプラズマのシーンの最後で、オリジナルと
  同じように下にスライドアウトして終わらない」
- 「PLZPART のスライドアウトが重く移動がカクカクしている。
  ハードウェアでスクロールできないのか？」

**原作の根拠:**
- `reference/SecondReality/PLZPART/PLZ.C:94`
  `if(curpal==5 && cop_drop>64) break;`
  → 最後の色切り替えで `cop_drop>64` になった時点で `plz()` を抜ける。
- `reference/SecondReality/PLZPART/COPPER.ASM` の `do_drop`:
  `dtau[c] = c*c/4*43/128+60` を VGA の line compare (CRTC 表示開始)
  に書き込み、表示を下へ下げる。描画側は動かさない。
- `PLZ.C:119-121` は最後に `set_plzstart(500); cop_plz=0;`。
- そのあと `VECT.C` の `getspl()` がキューブを画面外 (ty=2000) から
  スライドインする。

**実装:**
- `src/m2/framework/video.c/.h` に `video_set_display_start(int y)` を追加。
  - 標準画面の `DIWSTRT` の VSTART を `0x2C + y` に下げる
    (`0x2C81` が通常)。これが Amiga のハードウェア縦シフト。
  - `y > 211` で `DIWSTRT` の 8bit 範囲を超えるため、`zero_row`
    (チップ RAM の 0 の 1 走査線) に全プレーンを向け、`BPLxMOD=-40`
    にして残り 45 行を黒にする。
  - 両方の銅リスト (front/back) を更新する。
- `src/m2/parts/plasma.c`:
  - `plasma_vbl_tick()` を追加し、`dis_setcopper(3, plasma_vbl_tick)` で
    毎 VBL 呼ぶ。これで描画が 17fps でもスライド位置は 50Hz で滑らかに
    動く (原作の line compare と同じ考え方)。
  - `WIPE_VBLS = 40`:
    原作の dtau は 56 フレームで画面外 (263 > 256 行)。
    `56 * 50/70 = 40 VBL`。
  - `END_WIPE_VBLS = 46`:
    原作は `cop_drop > 64` (65 フレーム) で終了。
    `65 * 50/70 = 46.4 → 46 VBL`。
  - スライド量: `off = (w*w*49*43) / (25*512);`
    (`dtau[c]` から 60 を引いた分。`w` は 50Hz VBL。
    `c = w*7/5` を整数に切り捨てると VBL ごとの増加が
    1,1,2,1,2,... と不揃いになり高速部でカクつくため、
    `(w*7/5)^2` を展開して 1 回だけ割る。4287380 で修正)。
  - 終了スライドは `t + 46 >= CUBE_START_VBL` で開始。
    `CUBE_START_VBL=1900` なので 1854 VBL から開始、1900 でキューブ。
  - `wipe_done` でパレット切り替えを描画側が適用。
    終了スライドの場合は `end_done` を立てて黒のままキューブへ。
  - キューブ開始時に `dis_setcopper(3, 0)` と
    `video_set_display_start(0)` で後始末。
- `src/m2/main.c` のシーン切替時にも
  `video_set_display_start(0)` と `dis_setcopper(3, 0)` を追加
  (プラズマ中に右クリックで飛んだ場合の後始末)。

**実測 (重要):**
- 一時的に PLZPART に VBL/描画カウンタのデバッグバーを出して計測
  (コードは削除済み)。
  - host 2.048s: VBL=51, render=15
  - host 10.097s: VBL=451, render=148
  - host 18.141s: VBL=851, render=288
  - VBL は 49.7/s でほぼリアルタイム。プラズマ描画は 68030-max で
    **約 17fps**。したがってソフトウェアで表示位置を動かす旧実装は
    カクカクしていた。ハードウェア縦シフトで解決。
- 終了スライドは確認済み (キャプチャは整理のため削除):
  - プラズマが下へ抜けて黒になる
  - そのあとキューブが画面下 (top=398) から入ってくる

**既知の制限:**
- `DIWSTRT` は 8bit なので、シフトは最大 211 行。残り 45 行は
  `zero_row` で黒にする。最後の 45 行が約 0.1 秒早く消えるが、
  その時点の dtau は最も速い部分なので実害は少ない。
- 完全に 256 行まで滑らかに出したい場合は、PLZPART 専用の
  512 行カスタム画面 (上下に黒パディング) を作り、
  `BPLxPT` の表示開始行を VBL で動かす方法がある (未実装)。

### 1.3 DOTS の開始フェード (dots.c)

- 原作 `reference/SecondReality/DOTS/MAIN.C:150-183`:
  - パレットを全 0 にする。
  - 背景を描く。
  - `b=64..0` の 65 段階で `pal2[c]=pal[c]-b` を作り、2 VBL ごとに
    設定してフェードインする。
- 移植版は `make_palette()` でいきなり完成パレットを出していた。
- 修正:
  - `pal_out[768]` を追加。`dots_init()` で黒を設定。
  - `dots_render()` で `start_fade_step` を 10/7 VBL ごとに進める
    (70Hz の 2 VBL = 50Hz の 10/7 VBL)。
  - フェード中は背景だけ描き、玉のシミュレーションは進めない。
  - フェード完了後に `palette` を設定し、`el=1` で玉を開始。
- 検証済み: 黒 → 背景グラデーション → 玉の順を確認
  (キャプチャは整理のため削除済み)。

### 1.4 ツール修正

- `run/build-fixed.ps1`:
  - BOM なし UTF-8 + 日本語コメントの PowerShell で、コメント末尾の
    多バイト文字が改行を飲み込み、次の行 (`(Get-Item "main.c")...`) が
    コメント扱いになっていた。コメントを ASCII に変更。
  - シーン番号の一覧を現在の 20 シーンに更新。
- `run/sample-frames.ps1`:
  - 同じ日本語コメント問題を ASCII コメントに変更。
  - WinUAE のウィンドウがまだ出ていないときのリトライ (最大 30 秒) を追加。

## 2. 現在の未完了項目

- 実機での確認待ち: 2回目の起動、ENDSCRLの開始、PANICENDの帯、
  MNTSCRLの開始/終了、Rotazoom/PLZPARTの進行。
- U2Eは変更前後の実機profが別実行のため厳密なA/Bではない。U2Aの変更前値、
  PLZPART/MINVBALLの変更前値も未取得。現行prof値と比較上の注意は上記を参照。
- U2A/U2Eのポリゴン描画境界変更後の、実機表示と原作の画素比較は未記録。
- 最新profで重い候補は MNTSCRL / RAYSCRL / U2E / TUNNELI / 3DSINFLD。
  平均1回の計測だけで変更せず、同一条件で再測定してから対象を選ぶ。
- 音楽が原作動画より約0.7〜0.8%遅い件は仮説段階
  (原作 `U2.ASM:737-743` の再生レート丸め)。変更はユーザー判断待ち。
- 古い記録にあるJPLOGOの `el` キャップ懸念は、68020向けエミュレータでは
  検証しない。再調査する場合は68030以上の実機で時間を測り、原作行と
  C++版の値を照合するまで値を変更しない。

## 3. 過去の検証手順 (実行前に最新状態とAGENTS.mdを確認)

1. 検証キャプチャは整理のため削除済み。WinUAE は停止済み、
   `m2.exe` は通常ビルドに戻してある。`run/m0.hdf` も削除したが、
   `run/run-m0.ps1` が `m2.exe` から自動で作り直す。
   必要なら `run/build-fixed.ps1 -Scene N` で固定シーンに切り替える。

2. 再確認したい場合は固定シーン 10 で再キャプチャする:
   - 72.8s 付近で Cube が即黒
   - 74.4s 付近から DOTS 背景
   - 75.5s 付近から玉

3. 固定シーンで BEGLOGO / GLENZ を再確認:
   ```
   .\run\build-fixed.ps1 -Scene 3
   .\run\run-m0.ps1
   .\run\sample-frames.ps1 -OutPrefix be -StartDelaySec 0 -Count 30 -IntervalMs 200
   ```
   その後 `-Scene 4` で GLENZ の zoomer2 を確認。
   - 白背景 (タイトル)
   - 黒い帯が上下から閉じる
   - パレットが灰色へ
   - 最後に黒 (灰色の全画面が残らない)

4. JPLOGOの速度を再調査する場合は68030以上の実機を使い、固定シーン14の
   スライド開始からズーム完了までを実時間と `vblank_frames()` で測る。
   原作にキャップが無いことだけを理由に移植側の値を変えず、
   AGENTS.mdの根拠・測定手順を先に満たす。

5. 通常ビルドに戻して通し確認:
   ```
   .\run\build-fixed.ps1 -Normal
   .\run\run-m0.ps1
   ```
   ALKU から右クリックで PLZPART / DOTS まで飛ばし、
   シーン切替で画面が縦にずれたままにならないこと、
   ENDSCRL の括弧、DOTS のフェードを確認。

6. commit & push。推奨メッセージ:

   ```
   ENDSCRL: restore the two 0x8f font glyphs so (C) is not shifted;
   PLZPART: original downward slide-out at the end, driven by the VBL
   handler with hardware DIWSTRT scrolling; DOTS: original black start
   fade-in
   ```

## 4. 検証コマンド・ツール

- ビルド: `src/m2` で `gnumake -j8`
- 固定シーン: `run/build-fixed.ps1 -Scene N`
  - 0 alku / 1 u2a / 2 pam / 3 image / 4 glenz / 5 tunnel /
    6 techno / 7 panic / 8 forest / 9 lens / 10 plasma / 11 dots /
    12 water / 13 sinus / 14 jplogo / 15 ddstars / 16 end /
    17 scroll / 18 u2e / 19 credits
- HDF 作成 + 実行: `run/run-m0.ps1`
  - `run/m0.uae` は 68030-max。これは68060実機の性能測定の代替ではない。
    68020は対象外。実機性能は68030/68060の実機profで測る。
- 連続キャプチャ:
  ```
  .\run\sample-frames.ps1 -OutPrefix xx -StartDelaySec 0 -Count 60 -IntervalMs 200
  ```
  - `run/xx_times.csv` にホスト時刻、`run/xx0000.png` ... を保存。
  - ウィンドウは 736x631。Amiga 320x256 画面は
    キャプチャ座標 x=66..702, y=65..577 付近 (2 倍)。
  - **PNG をモデルに渡さない。** Python + PIL で
    平均輝度・行プロファイル・bbox を数値で出す。
- 画面の停止: `Stop-Process -Name winuae-gdb -Force`
- 画像解析の例:
  ```
  python -c "from PIL import Image; im=Image.open('run/xx0010.png').convert('L'); ..."
  ```

## 5. 注意点

- `run/m0.uae` を一時的に差し替えた場合は**必ず戻す**。
- `run/build-fixed.ps1` は `main.c` のタイムスタンプを更新して
  `CCFLAGS` を変える。ビルド後は `src/m2/out/m2.exe` のサイズと
  タイムスタンプを確認する。
- `video_set_display_start` は標準画面の銅リストを書き換える。
  PLZPART 以外では呼ばない。カスタム画面 (ENDSCRL/ALKU) と
  同時に使わない。
- `dis_setcopper(3, ...)` は現在 PLZPART だけが使う。
  main のシーン切替で必ず 0 に戻す。
- 70Hz → 50Hz の換算を忘れない:
  - 56 フレーム → 40 VBL
  - 65 フレーム → 46 VBL
  - 2 VBL → 10/7 VBL
- 最後に `git status` で未コミットがないことを確認し、push する。

---

## 付録: 現在のコードの要点

- `src/m2/framework/video.c`
  - `diwstrt_slot[2]`, `zero_row`, `display_black`
  - `set_standard_pointers()`
  - `video_set_display_start(int y)`
- `src/m2/parts/plasma.c`
  - `plasma_vbl_tick()`
  - `WIPE_VBLS 40`, `END_WIPE_VBLS 46`
  - `wipe_active`, `wipe_done`, `end_wipe`, `end_done`
  - `CUBE_START_VBL 1900`
- `src/m2/parts/dots.c`
  - `pal_out`, `start_fade_step`, `start_fade_acc`, `start_fade_done`
- `src/m2/parts/scroll.c`
  - `scan_font()` の `o` に `\x8f\x8f`
- `src/m2/main.c`
  - シーン切替時の `video_set_display_start(0)` /
    `dis_setcopper(3, 0)`
