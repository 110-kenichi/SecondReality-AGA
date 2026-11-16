# Claude Code 用 最初のプロンプト (コピペ用)

以下をそのまま Claude Code に貼り付ける。

---

このリポジトリ (SecondReality-A1200) の続きをやってください。
まず `AGENTS.md` と `docs/HANDOFF_CLAUDE.md` を熟読してください。
このリポジトリは Second Reality の Amiga A1200 移植で、PAL 50Hz 固定、
原作 DOS 版は 70Hz です。想像で実装せず、原作
(`reference/SecondReality/...`) のファイル名・行番号か、C++ 版
(`XorJS/SecondRealityPlusPlus` の `Parts/...cpp`) の行番号、
または実測値を根拠にしてください。70Hz→50Hz の換算は式をコメントに
残してください。画像はモデルに渡さず、Python + PIL でピクセル解析して
数値で判断してください。

最新の状態 (2026-11-15): `docs/HANDOFF_CLAUDE.md` 冒頭の「最新の状態」を
読んでください。目標は実機 (68060 / 68030) で動くことで、速度は
`data/prof.cfg` を置いて実機で取る `data/prof.txt` で判断します。
音楽位置の待ちは「到達または通過」で書いてください (AGENTS.md)。

以下は 2026-11-14 時点の古いメモです。
現在の状態:
- HEAD は `f00a629`。**未コミットの変更あり**。内容は
  `docs/HANDOFF_CLAUDE.md` の「1. このセッションでやったこと」に
  全部書いてあります。
- `src/m2/out/m2.exe` は `TEST_FIXED_SCENE=10` (PLZPART) の
  テストビルドです。
- PLZPART の Cube 終端 + DOTS 遷移は解析済みです。
  Cube は 72.8s 付近で**フェードせず即座に黒**になり、そのあと
  DOTS が黒からフェードインします。原作 `VECT.C` に Cube の
  フェードアウトは無いので、Cube を独自にフェードさせる変更は
  しないでください (ユーザーに説明する)。
- 検証キャプチャ (PNG/CSV) と `run/m0.hdf` は整理のため削除済みです。
  `run/run-m0.ps1` が HDF を自動で作り直します。

今回の変更の要点:
1. ENDSCRL の `Copyright (C)` が `Copyright 'C'` になる問題を修正。
   原作 `ENDSCRL/MAIN.C:13` の `fonaorder` にある 0x8f 2 文字を
   `scan_font()` の文字列に追加。
2. PLZPART の最後に、原作 `PLZ.C:94` / `COPPER.ASM do_drop` どおりの
   下方向スライドアウトを追加。描画側ではなく
   `video_set_display_start()` で `DIWSTRT` を毎 VBL 下げる
   ハードウェア縦シフトにした (`plasma_vbl_tick`)。
   70Hz 56 フレーム → 40 VBL、65 フレーム → 46 VBL。
3. DOTS の開始フェード (原作 `DOTS/MAIN.C:150-183`) を追加。
4. `run/build-fixed.ps1` / `run/sample-frames.ps1` の
   PowerShell 日本語コメントによる行飲み込みバグを修正。

残りの確認・作業:
- Cube 終端 → 黒 → DOTS フェードインは確認済み。
  **原作 `VECT.C` には Cube 自体のフェードアウトは無い**ので、
  Cube をフェードさせる独自効果は追加しないこと。ユーザーには
  「原作は DOTS が黒からフェードインする」と説明する。
- 固定シーン 3 (BEGLOGO) / 4 (GLENZ) で、タイトルの白背景、
  zoomer2 の灰色フェード、直後の黒 (灰色全画面が残らない) を確認。
- `run/m0-68020.uae` で JPLOGO のスライド＋ズーム速度を実測。
  `jplogo.c` のスライドは `el` を 6 でキャップしているため、
  8.3fps 未満だと実時間より遅くなる可能性がある。原作
  `JP.C:212-219` にキャップは無い。必要ならキャップを外す。
- 通常ビルドに戻して通し確認し、commit & push。

検証が終わったら必ず commit & push してください。
