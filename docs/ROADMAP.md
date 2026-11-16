# Second Reality → Amiga 1200 移植ロードマップ

> 対象: 1993年 Future Crew「Second Reality」(開発コード名 Unreal 2)
> ソース: [mtuomi/SecondReality](https://github.com/mtuomi/SecondReality) (Unlicense / パブリックドメイン)
> ターゲット: Amiga 1200 + アクセラレータ (68030/68040, AGA, Fast RAM), PAL 50Hz
> 検証環境: WinUAE (A1200 + 68030 + 8MB Fast RAM + AGA, 内蔵 AROS ROM)

---

## エグゼクティブサマリ

- **実現可能性: 高い**。ソースは Unlicense (パブリックドメイン) で、画像資産の大半が
  Amiga ネイティブの ILBM (LBM) 形式。音楽も標準 S3M 版が入手可能 (構造一致を検証済み)。
- **最大の技術課題は 3 点**:
  1. 8 ビットプレーン (256 色) の chunky→planar 変換 (c2p) の性能確保
  2. STMIK (ソース非公開) の置き換え = Paula 4ch 向け S3M リプレイヤの新規実装
     (音楽は 8 ボイス/4 ステレオペア構成で Paula と相性が良い)
  3. 70Hz 前提の演出を 50Hz (PAL) に再調整
- **推奨構成**: AGA 320x256 / 256 色 / ダブルバッファ + Copper、
  自作 S3M リプレイヤ (libxmp を参考) + Paula 4ch 出力 + STMIK 互換 API、
  開発は WSL2 + amiga-gcc、検証は WinUAE。
- **移植の山場**: VISU (U2A/U2E スペースバトル)、GLENZ (256 色 chunky の XOR 透明合成)、
  PLZPART (プラズマキューブ)、TECHNO (CRTC/プレーン技巧)。ここに全体の半分以上の工数を見込む。

---


## 0. この文書について

本リポジトリの調査(ソース構造・データ資産・音楽の実測)と Amiga 側技術の選定結果に基づき、
移植の実装方針・パート別難易度・マイルストーンをまとめたものです。

用語:

- **オリジナル**: 1993 年の DOS 版 Second Reality / そのソース
- **A1200**: Amiga 1200 + アクセラレータ (本計画のターゲット)
- **DIS**: Demo Interrupt Server (オリジナルの常駐デモ制御サーバ)
- **STMIK**: Scream Tracker Music Interface Kit (オリジナルの音楽再生ライブラリ)

---

## 1. 調査サマリ (オリジナル側)

### 1.1 全体構成

- 20 パートを個別 EXE として開発し、ローダ (`MAIN`) が順に実行する構造。
  - パート一覧は `MAIN/PARTS` に定義 (Alkutekstit I〜III, Glenz, Dottitunneli, Techno,
    Panicfake, Vector Part I - Space battle, Peilipalloscroll, Desert Dream Stars,
    Lens, Rotazoomer, Plasma, Plasmacube, MiniVectorBalls, Vuori-Scrolli,
    3D-Sinusfield, Vector Part II - KewlComplex, Endpictureflash, Credits)
  - 実行ファイルは `MAIN/DATA/*.EXE`。ベクターパート (`U2A`, `U2E`) は
    `U2A.001`〜`U2E.0AB` に分割され、ローダが結合する。
- 実装言語: Borland C++ (.C) + Turbo Assembler (x86 .ASM/.INC)。
  ホットスポットはほぼ x86 アセンブリ。
- 実行環境: 386 以上、コンベンショナルメモリ 600KB + (SB 使用時) EMS 1MB。
  サウンドは AdLib / Sound Blaster / Gravis UltraSound。
- ビデオ: VGA Mode 13h / Mode X (320x200x256)。`waitborder` (垂直帰線待ち) を
  パート同期の基本とし、パレット/CRTC/copper 的なラスター効果を使用。
- 音楽: Scream Tracker 3 (S3M) を STMIK ライブラリで再生。
  パートは `np_row` / `np_tempo` 等の now-playing 情報を参照して演出を同期する。

### 1.2 データ資産の実測

| 種類 | 場所 | 備考 |
|---|---|---|
| 画像 (大半) | 各パートの `*.LBM` | Deluxe Paint の **ILBM**。Amiga ネイティブのプラナー形式で、資産面は大きなアドバンテージ |
| GIF | `PICS/`, `FOREST/` | オフライン変換が必要 |
| アニメ | `PAM/PRAX4.FLI` (335KB) | Autodesk Animator FLIC (8bit チャンキー) |
| 3D データ | `3DS/` (`*.3DS`, `*.VUE`, `*.ASC`, `*.PRJ`) | 3D Studio 由来。各パートには変換済みバイナリ (`*.INC`/`*.ASM`) も存在 |
| フォント | `GRAB/FONT/`, `GRAB/SYSFONT/`, 各パート `FONA*.INC` | LBM または変換済みデータ |
| 音楽 | `MAIN/MUSIC0.S3M` (391KB), `MAIN/MUSIC1.S3M` (601KB) | 下表参照 |

### 1.3 音楽の実測結果 (重要)

| 項目 | MUSIC0 | MUSIC1 |
|---|---|---|
| 曲名 | UnreaL ][ - The 2ND Reality | UnreaL ][ / PM |
| サイズ | 391,312 bytes | 600,860 bytes |
| オーダー数 | 80 (255=終端, 254=スキップ) | 100 |
| インストゥルメント | 44 | 54 |
| パターン | 77 | 77 |
| 使用チャンネル | **8ch (0〜7)** | **8ch (0〜7)** |
| 初期 speed/tempo | 6 / 125 | 3 / 130 |

- 1 モジュール内に複数セクションを持ち、パート側が「オーダー/行」を指定して
  途中から再生する使い方 (STMIK の `zgotosong(order,row)` 相当)。
- **注意**: リポジトリの 2 ファイルは、ヘッダ・インストゥルメント・サンプル波形が
  標準 S3M と完全一致する一方、**パターンデータ領域のみが標準 ST3 パッキングと
  異なる符号化**(高エントロピー、行終端 0x00 がほぼ無い)になっている。
  ModArchive に同構造・同サイズの標準形式版 (`2nd_skav.s3m` / `2nd_pm.s3m`) が
  存在し、こちらは通常の S3M として解釈できることを確認済み。
  → 移植では標準形式版を入力に使う(または初期 ST3 形式のデコーダを後日解明)。

### 1.4 パート別技術分析

各パートの実装方式・データ・移植難易度の詳細は **セクション 4** にまとめた。

### 1.5 ローダ / DIS / パート実行の仕組み (実装上の要点)

- `U2.EXE` = ローダ本体。**DIS を静的にリンク**し、`INT 21h` フックで
  パックファイル (`SECOND.EXE` 末尾、`PACKING.INC` に 79 ファイルの目録) から
  ファイルを透過的に供給する。パートは **1 本ずつ子 EXE として実行**され、
  パート単位のメモリ制限は約 450〜600KB。
- **DIS の 11 サービス** (パートは `int 0FCh` で呼ぶ):
  `version / waitb / exit / indemo / loader / msgarea / muscode / setcopper /
  fastvmode / musframe / sync`
  - `waitb` は「経過フレーム数」を返す (パート側の `frame += dis_waitb()` による
    フレーム落ち補正に使われる) — この意味論は Amiga 側でも維持する。
  - `muscode` は zinfo / row / plus (次のマーカーまでの行数) を返し、
    パートはこれで演出を音楽に同期する。
- **音楽の再開位置** (セクション遷移時にローダが指定):
  MUSIC0 = orders 0 / 18 / 25 / 70、MUSIC1 = orders 0 / 42。
- パート表 (`U2.ASM`)・再開オーダー・`ordersync1` (order,row → パート番号) は
  **純粋なデータとして再利用可能**。
- **COMAN (3D-Sinusfield) を最初のパイロットパートに推奨**。
  パート骨格・パレット・ページ切替・生成ループを一通り含むため。

---

## 2. Amiga 側の技術戦略

### 2.1 画面 (ビデオ)

- **表示モード**: AGA 8 ビットプレーン (256 色, 24bit パレット)。
  PAL の表示可能領域は 320x256。オリジナルは 2 系統の画面を使い分けている:
  - 320x200 256 色 (Mode 13h / チェーン4 オフの chunky): GLENZ, TECHNO, DDSTARS など
  - 320x400 / 640x400 プラナー (Mode X 系): ALKU, BEG, END, PANIC, ENDSCRL など

  本計画は **320x256 フル (PAL 50Hz)** を基本とし、400 ライン系のパートは
  (a) 320x256 へ再レイアウト、(b) インターレース、(c) 31kHz ダブルスキャン
  (VGA モニタ前提) のいずれかで対応する。TV/15kHz 環境を優先するなら (a) を推奨。
- **ダブルバッファ**: 2 面のビットマップを Copper で切り替え、VBlank 待ちで同期。
- **描画方式のハイブリッド**:
  1. **直接プラナー描画**: Glenz・ベクター・ライン・塗りつぶし・コピー系。
     Blitter (コピー/矩形塗り/ライン/マスク) と CPU のビット操作を併用。
  2. **チャンキー + c2p**: プラズマ・トンネル・ウォーター・星・レンズなど
     ピクセル単位の効果。320x200 の chunky バッファに描き、フレーム毎に
     c2p (chunky→planar) で 8 プレーンへ変換。
- **c2p の候補** (いずれも 68k 向け実装が入手可能):
  - `c2p.library` (MIT, 共有ライブラリ実装)
  - `c2p-68k` (MIT, ポータブル C + 最適化解説)
  - `c2pAmiga` (Blitter ベース, A500 向け)
  - 古典的な Kalms 系 / Zerkman 系ルーチン
- **性能見積り**: 320x200 = 64,000 ピクセル。68030/40 では実装次第で
  おおむね 2〜6 cycle/px。**5〜15ms/フレーム**を見込む。
  50Hz の予算は 20ms なので、重いパートでは
  「c2p は毎フレーム行わない」「解像度を落とす」「直接プラナー化」等の調整が必要。
- **Copper**: パレット分割・ラスター効果は Amiga の得意分野。
  オリジナルの CRTC/パレットトリックは Copper に置き換えることで再現/強化できる。

### 2.2 音 (Paula / S3M)

**オリジナルの実態**:
- 音楽は S3M 2 モジュール。**8 ボイスだが、パン設定は 4 ステレオペア (L/R 固定)**
  — Paula の 4ch と相性が良い。
- 再生は STMIK だが、**ソースは非公開** (24,849B のプリビルト OMF オブジェクト、
  x86 asm で PIT/DMA/SB/GUS を直接制御)。→ **移植ではなく再実装**が必要。
- ミックスレート: 12/16/20 kHz (SB Pro ステレオ時は 2 倍)。70Hz で `zpollme`。
- 同期: パートは DIS 経由で `np_zinfo` / `np_row` / `np_zplus` (order list の
  254 マーカー) / `np_zframe` を参照する。

**方式比較**:

| 方式 | 内容 | 長所 | 短所 | 評価 |
|---|---|---|---|---|
| A. Paula 4ch + ペア毎 2:1 ミックス | 8 ボイスを Paula 4ch へ 2 ボイスずつ割り当て、各 ch 内でミックス | Paula の音色を活かせる。CPU 中程度。音質良好 | リプレイヤの新規実装が必要 | **推奨** |
| B. ソフトウェアミックス → Paula 2ch (14bit) | 8ch を L/R の 14bit ストリームにミックスして 2ch で再生 | STMIK の挙動に最も近い。実装が素直 | CPU 消費大。14bit は実機キャリブレーション要 | 次点 |
| C. 4ch ダウンミックス + ptplayer | OpenMPT で 4ch 化し、軽量 MOD リプレイヤ (ptplayer) で再生 | CPU ほぼゼロ。実装容易 | 8→4 の音の欠落 | 軽量フォールバック |
| D. 事前レンダリング PCM | PC 側で PCM 化してストリーム再生 | 再現度最高 | 容量大。同期 API 喪失 | 最終手段 |

- **リプレイヤ実装方針**: モジュールのパース/再生ロジックは libxmp (MIT) を
  参考または移植し、**出力段は Paula 専用ドライバを自作**する。
  STMIK 互換 API (`zplaysong(order)`、`np_ord/np_row/np_zinfo/np_zplus/np_zframe`)
  を実装し、パート側の同期コードをほぼそのまま使えるようにする。
- **同期検証**: オリジナルを DOSBox 等で動かして `np_*` の推移をログし、
  Amiga 側リプレイヤと突き合わせる。`ordersync1` (order,row→パート) は再利用。
- **サンプル**: 8bit (pack=1 の Amiga デルタ形式を含む)。Chip RAM に配置。
  両曲で約 1MB になるため、セクション毎のロードで節約する。
- **Paula 14bit 出力 (方式 B 用)**: 2ch をペアで使い音量/周期を変調する古典手法。
  実機ではキャリブレーションが必要 (WinUAE では不要)。まず 8bit で開発し、
  後で 14bit 化する段階的アプローチが安全。
- 参考: ptplayer (4ch MOD リプレイヤ, Unlicense)。

### 2.3 タイミング / フレームレート

- オリジナルは 70Hz (VGA モード) の `waitborder` を基本同期としている。
- DIS の `waitb` は**経過フレーム数を返す**仕様 (パートは `frame += dis_waitb()`
  でフレーム落ち補正を行う)。Amiga 側フレームワークでもこの意味論を維持する。
- A1200 は PAL 50Hz。**フレーム数依存の演出は係数を再調整**する。
  音楽同期 (row/tick 単位) は BPM を維持したまま 50Hz に載せ替えられる。
- パート間遷移はハードカット (ローダレベルに遷移演出は無い)。各パートが
  自前でフェードする。`ordersync1` (order,row → パート番号) を再利用する。

### 2.4 CPU / メモリ

- **目標スペック**:
  - 最低: 68030/25 + Fast RAM 8MB (2MB Chip)
  - 推奨: 68040/40 + Fast RAM 16MB
- **メモリ配置**:
  - Chip RAM: 画面 (8 プレーン x2 面 = 各 64KB)、オーディオバッファ、Copper リスト
  - Fast RAM: コード、パートデータ、chunky バッファ、サンプル
- オリジナルの「450KB/パート + EMS 1MB」制約より恵まれているが、
  `U2A`/`U2E` (ベクターパート) は分割データを大量に持つため、
  パート単位のロード/破棄 (オーバーレイ) を基本とする。

### 2.5 ツールチェーン / 検証環境

| 用途 | 採用 | 備考 |
|---|---|---|
| **クロスコンパイラ (採用)** | `vscode-amiga-debug` (Bartman/Abyss) 同梱ツールチェーン | **GCC 15.1 (m68k-amiga-elf) + binutils + elf2hunk + vasm + gnumake**。VSIX を展開して使用 (M0 で実績あり) |
| 代替 | WSL2 (Ubuntu) + amiga-gcc (Bebbo toolchain) | 上流 GitHub (`bebbo/amiga-gcc`) は現在 404。フォークや Docker (`amigadev/crosstools:m68k-amigaos`) は健在 |
| 代替 (軽量) | VBCC + vlink | C コンパイラのみで軽量 |
| **エミュレータ (採用)** | 同梱の改造版 WinUAE (`winuae-gdb.exe`) | **内蔵 AROS ROM** で起動 (KS 3.1 は提供後に差し替え)。GDB/プロファイラ連携付き |
| ビルド | 同梱 gnumake + Makefile | テンプレート準拠 (`src/m0/Makefile`) |

---

## 3. 移植アーキテクチャ (案)

```
SecondReality-A1200/
├── docs/                … 本ドキュメント
├── tools/               … PC 側データ変換ツール (Python)
│   ├── lbm2amiga.py     … LBM/ILBM → Amiga 用 (パレット 6→8bit 変換)
│   ├── fli2planar.py    … FLIC → フレーム列 (planar)
│   ├── 3ds_export.py    … 3DS/VUE → 自前バイナリ
│   └── s3m_prepare.py   … 標準 S3M の取得/検証/再パック
├── src/
│   ├── framework/       … 新フレームワーク (ローダ/シーン/遷移)
│   ├── video/           … 画面, パレット, Copper, Blitter, c2p
│   ├── audio/           … S3M リプレイヤ + Paula ドライバ + 同期 API
│   ├── input/           … キーボード/ジョイスティック
│   └── parts/           … パート別実装 (20 パート)
└── build/               … 中間生成物
```

### 3.1 オリジナル資産の再利用方針

| オリジナル | Amiga 側 | 方針 |
|---|---|---|
| LBM (ILBM) | planar ビットマップ | ほぼそのまま。パレットは 6bit→8bit (値<<2) で無損失 |
| `*.INC`/`*.ASM` 変換済みデータ | バイナリブロブ | 形式を確認し、そのまま埋め込み可 (エンディアン注意) |
| 3DS/VUE/ASC | 自前形式 | PC 側で変換。または変換済みデータを再利用 |
| FLIC | planar フレーム列 | PC 側で変換 (実行時デコードも可) |
| S3M | 標準 S3M | ModArchive 版を入力。リプレイヤは自作 (libxmp を参考) |
| x86 アセンブリ | 68k アセンブリ or C | アルゴリズムを移植。ホットスポットのみ 68k asm |

### 3.2 DOS 依存の置換表

| オリジナル | 置換先 |
|---|---|
| DIS (TSR, 割り込み) | フレームワーク内のシーン管理 + VBlank ハンドラ |
| `waitborder` (VGA 帰線) | Copper/VBlank 待ち |
| VGA ポート/CRTC | Copper リスト + AGA レジスタ |
| AdLib/SB/GUS ドライバ | Paula + S3M リプレイヤ |
| EMS/XMS, far pointer | 68k フラット 32bit アドレス空間 (Fast RAM) |
| EXE ロード/パッキング | 自前ローダ (パート単位) |
| FCP (Future Protector IV, EXE 暗号化) | 不要 (Amiga は hunk 実行形式。必要なら Amiga 製クランチャ) |
| UTIL / DOOBJ (データ→OBJ 埋め込み) | `incbin` / `objcopy` 相当に置換 (Amiga 側では不要) |
| GRAB ツール群 (LBM/FLI/3DS/FCP 変換, TSR グラバ) | PC 側のオフライン変換ツール (Python) に置換。Amiga では実行しない |

---

## 4. パート別移植難易度

### 4.1 パート / ディレクトリ / 実行ファイル対応表

`MAIN/U2.ASM` のロード順と `MAIN/DATA` の EXE を正として整理 (全ディレクトリ調査済み)。

| 元ディレクトリ | 実行ファイル | デモ内の位置づけ | 難易度 |
|---|---|---|---|
| ALKU | ALKU.EXE | イントロテキスト I〜III (フェード+スクロール) | Medium |
| BEG | BEGLOGO.EXE | タイトルロゴ (320x400 フェードイン) | Easy |
| START | (未収録, S.EXE) | イントロの試作 (最終版では未使用) | Easy |
| TECHNO | TECHNO.EXE | Techno (干渉円/ワイヤーフレーム/トロール画像) | Hard |
| GLENZ | GLENZ.EXE | Glenz (透明ポリゴン、256色 chunky XOR 合成) | Very Hard |
| DDSTARS | DDSTARS.EXE | 隠しパート: 3D スターフィールド + テキストバナー | Medium–Hard |
| PLZPART | PLZPART.EXE | Plasma + Plasmacube | Hard (cube) / Medium (plasma) |
| TWIST | (未収録, T.EXE) | ツイスター実験 (最終版では未使用) | Easy |
| DOTS | MINVBALL.EXE | MiniVectorBalls (512 ドットの物理演算) | Easy |
| TUNNELI | TUNNELI.EXE | ドットトンネル (Turbo Pascal + テーブル) | Easy–Medium |
| GRID | (p.exe, 試作) | 3D-Sinusfield の試作。**製品版 3DSINFLD.EXE は COMAN からビルド** | Medium (製品版の難易度は COMAN 参照) |
| LENS | LNS&ZOOM.EXE | Lens + Rotazoomer | Medium |
| PAM | PAM.EXE | Praxis 爆発 FLI 再生 (41 フレーム) | Medium |
| END | ENDLOGO.EXE | エンドピクチャのフラッシュ/フェード | Easy–Medium |
| ENDSCRL | ENDSCRL.EXE | グリーティング縦スクロール (640x400x16) | Medium |
| CREDITS | CRED.EXE | クレジット (分割画面+パン、生成データ欠落に注意) | Hard |
| PANIC | PANICEND.EXE | モンスターのロール/メルト演出 | Medium–Hard |
| HARD | (未収録, B.EXE) | Glenz の試作 (ドットロゴ/グリッド/透明キューブ) | Medium–Hard |
| VISU (U2A) | U2A.EXE | Vector Part I - スペースバトル | Hard |
| VISU (U2E) | U2E.EXE | Vector Part II - KewlComplex | Hard |
| WATER | RAYSCRL.EXE | Rayscroll (レイトレース水面 + オフセットリスト) | Easy–Medium |
| FOREST | MNTSCRL.EXE | Vuori-Scrolli (山スクロール、加算マスク) | Easy–Medium |
| JPLOGO | JPLOGO.EXE | Ice Kingdom 回転シリンダー | Medium–Hard |
| ENDPIC | BEGLOGO.EXE | タイトルカード (BEG の簡易版) | Easy |
| COMAN | 3DSINFLD.EXE | 3D-Sinusfield (製品版、パイロット候補) | Medium / High (生成ループ) |
| MAIN / DIS | (ローダ/音楽/中断サーバ) | ローダ、STMIK 音楽、DIS (デモ内は静的リンク) | Medium (ローダ/DIS) / High (音楽) |

### 4.2 難易度一覧 (全パート調査完了)

| ディレクトリ | パート / 画面 | 元の方式 | 主要データ | 難易度 | 移植の要点 |
|---|---|---|---|---|---|
| ALKU | イントロテキスト I〜III (フェード+スクロール) | 4 プレーン Mode X + DIS copper | HOI ≈130KB, FONT 46KB | **Medium** | 画像/フォント再利用可。CRTC スクロール→Bitplane ポインタ+Copper。XOR テキスト差分方式は移植可 |
| BEG | タイトルロゴ (白からフェードイン) | 320x400 Mode X | SRTITLE 45KB | **Easy** | 静止画+パレットフェード。Blitter コピー |
| START | イントロ試作 (未収録) | 同上 | HZPIC 61KB | **Easy** | 同上 |
| TECHNO | 干渉円 / ワイヤーフレーム / 画像スクロール | Mode X ページ + CRTC | CIRCLE 32KB, TROLL 71KB | **Hard** | CRTC/プレーン技巧の再解釈。ライン/クリップ算法は再利用可 |
| GLENZ | Glenz (透明ポリゴン) | 320x200 chunky XOR 合成 | FC 65KB + 作業 192KB | **Very Hard** | XOR 合成を AGA プラナーへ (Blitter XOR / CPU LUT)。自己書き換え asm・生成 zoom は書き直し。ポリゴン毎パレットは Copper 向き |
| HARD | Glenz 試作 (未収録) | chunky + 仮想ページ | FCRZ 64KB | Medium–Hard | GLENZ と同一ソース群 (include は GLENZ 側) |
| DDSTARS | 隠し: 3D スターフィールド + バナー | 2 プレーン OR 蓄積 + EMS | TEXT 16KB | **Medium–Hard** | EMS リング→Fast RAM リング。OR 蓄積は Blitter OR |
| PLZPART | Plasma + Plasmacube | Mode X プラズマ + テクスチャ面 | テーブル ≈150KB | **Hard (cube) / Medium (plasma)** | テクスチャ+シェード面の再実装が山。プラズマは chunky+c2p |
| TWIST | ツイスター試作 (未収録) | Mode X 焼き込み blitter | TMP 63KB | **Easy** | 行リマップのみ。68k で再生成 |
| DOTS | MiniVectorBalls (512 ドット) | Mode 13h chunky | 実行時生成 64KB | **Easy** | 整数物理+ドット。chunky+c2p or 4 プレーン直接 |
| TUNNELI | ドットトンネル | Mode 13h chunky (Turbo Pascal) | TUNNEL 34KB, SINIT 12KB | **Easy–Medium** | データ再利用可。Pascal→C。chunky+c2p |
| GRID | 3D-Sinusfield 試作 | chunky XOR ライン | EYE 64KB | Medium | ライン生成コード再生成。XOR は Blitter minterm |
| COMAN | 3D-Sinusfield (製品版 3DSINFLD.EXE, **パイロット候補**) | Mode X + 生成 386 ループ (THELOOP.INC) | W1/W2DTA 128KB, SIN1024 | **Medium (骨格) / High (生成ループ)** | 骨格/パレット/ページ切替は 1:1 で移植可。5,228 行の生成ループは 68k/C で再実装。データは再利用可 |
| LENS | Lens + Rotazoomer | chunky スパン + Mode X 回転 | 画像 64KB + 表 35KB + パス 19KB | **Medium** | レンズ表再生成。Blitter ロトズーマー |
| PAM | Praxis 爆発 FLI 再生 (41 フレーム) | Mode X ページ切替 | FLI 324KB + パレット | **Medium** | RLE デコーダ移植 (プラナー直書き可)。50Hz は 3 retrace/フレーム |
| VISU (U2A) | Vector Part I スペースバトル | 320x200 プラナー 4 ページ | メッシュ/アニメ/ソートリスト | **Hard** | 資産は全て再利用可。描画層 (Blitter 面塗り / Gouraud は CPU) を再実装。適応的フレームスキップ維持 |
| VISU (U2E) | Vector Part II KewlComplex | 同上 | 42 オブジェクト | **Hard** | 同上 |
| WATER | Rayscroll (水面) | Mode 13h オフセットリスト | WAT 74KB + 背景 65KB | **Easy–Medium** | オフセット列をプラナー向けに再生成 or LUT 変換 |
| FOREST | Vuori-Scrolli (山スクロール) | 同上 (加算マスク) | POS 75KB | **Easy–Medium** | 同上。加算はテーブル or chunky 変換 |
| JPLOGO | Ice Kingdom 回転シリンダー | 320x400 + 生成 zoom | 画像 59KB + zoom 318KB | **Medium–Hard** | 400→256 ライン再設計。行ズームは CPU |
| ENDPIC | タイトルカード (beglogo) | 320x400 RLE | SRTITLE 7KB | **Easy** | オフライン変換推奨 |
| END | エンドピクチャ (フラッシュ/フェード) | 320x400 RLE | PIC 46KB | **Easy–Medium** | 同上 |
| ENDSCRL | グリーティング縦スクロール | 640x400x16 プラナー | FONT 46KB + TXT 1KB | **Medium** | プラナー ビットマスク描画は Amiga 向き。400→256 |
| CREDITS | クレジット (分割画面+パン) | 320x200 4 ページ + line compare | 21 枚 + FONT | **Hard** | 生成データ欠落→再変換。分割/パンは Copper で再設計 |
| PANIC | モンスターのロール/メルト | 320x400 + ラッチコピー | MONSTER 64KB | **Medium–Hard** | Blitter コピー + Copper へ再設計 |
| MAIN / DIS | ローダ / 音楽 / 中断サーバ | DOS EXE ローダ + INT 21h + STMIK | S3M 2 曲 (8 ボイス/4 ペア) | **Medium (ローダ/DIS) / High (音楽)** | DIS 11 サービスをフレームワーク API へ移植 (`waitb` の経過フレーム返却仕様を維持)。パート表/再開オーダーは再利用。音楽は再実装 |

### 4.3 移植の共通要点

- **DIS (`int 0FCh`) の置換**: 全パートがフレーム待ち・終了判定・音楽同期・Copper コールバックを
  DIS 経由で行う。新フレームワークの VBlank ハンドラ + リプレイヤ同期 API に一本化する。
- **70Hz → 50Hz**: フレーム係数を ×50/70 (または音楽 row 同期へ載せ替え)。
  VISU/HARD は VBlank 同期のため、そのままだと約 29% 遅くなる。
- **chunky → planar**: 効果系は chunky+c2p、面塗り/ライン/XOR 系は Blitter・Copper を優先。
- **資産再利用**: メッシュ・法線・ソートリスト・アニメ列・オフセット表・パレット・画像は
  ほぼ全て再利用可能 (リトルエンディアン、形式は単純)。
- **自己書き換えコード/生成 x86 コード** (GLENZ, TECHNO, JPLOGO, TWIST 等) は
  翻訳せず、アルゴリズムから 68k 用に再生成する。

---

## 5. マイルストーン計画

| フェーズ | 内容 | 完了条件 | 期間目安 |
|---|---|---|---|
| M0 | 環境構築 | WSL + amiga-gcc で "Hello Amiga" が WinUAE で動く | 1〜2 週 |
| M1 | フレームワーク v0 | AGA 320x256 ダブルバッファ、VBlank、入力、パートロード | 2〜3 週 |
| M2 | 音 | STMIK 互換 API の S3M リプレイヤ (Paula 4ch) が動作し、np_* 同期が合う | 3〜5 週 |
| M3 | パイロット+簡単なパート (COMAN 3D-Sinusfield / 星 / プラズマ / エンド系) | 最初の「動くデモ」 | 4〜5 週 |
| M4 | 中規模パート (トンネル/レンズ/ドット/スクロール系/PAM/2D 系) | 半数以上のパートが単体で動作 | 6〜8 週 |
| M5 | 重量級 (VISU U2A/U2E, GLENZ, PLZPART, COMAN) | 全パートが単体で動作 | 8〜12 週 |
| M6 | 統合・遷移・最適化 | 通しで最後まで再生、030 でも実用速度 | 4〜6 週 |

※ 個人開発 + AI 支援を前提とした目安。実機検証は各フェーズ末。

---

## 6. リスクと対策

| リスク | 影響 | 対策 |
|---|---|---|
| 8bitplane c2p の CPU コスト | 重いパートで 50Hz が守れない | 直接プラナー描画の併用、c2p 頻度削減、解像度調整、030/040 差の吸収 |
| S3M リプレイヤの性能 | 音切れ・フレーム落ち | 16kHz 化、ミキサ最適化 (68k asm)、最悪 MOD 変換フォールバック |
| STMIK ソース非公開 (音楽) | 移植不可 | 再実装 (Paula 4ch + STMIK 互換 `np_*` API)。libxmp を参考に M2 で早期検証 |
| 70Hz→50Hz の演出差 | 見た目のテンポが変わる | 係数再調整、音楽同期は BPM 維持 |
| 3D データの再現 | 見た目が変わる | 変換済み `.INC` データを優先利用 |
| リポジトリ音楽の非標準エンコード | 音源が使えない | 標準形式版 (ModArchive, 同一構造を確認済み) を使用 |
| CREDITS の生成データ欠落 (`pic01.inc` 等が未収録) | ビルド不能 | `PICS/*.LBM` から再変換 (SFD.BAT 相当を Python 化) |
| 400 ライン系パートの再レイアウト | 工数/見た目 | 320x256 への再設計を基本、必要なら interlace/doublescan |
| 隠しパート・未収録 EXE | 完全再現の漏れ | `MAIN/DATA` の EXE 一覧と `MAIN/U2.ASM` のロード順を正として棚卸し |
| メモリ不足 (2MB Chip) | 一部パートが載らない | Chip/Fast の分離、パート単位ロード、バッファ再利用 |
| ライセンス | 公開時の問題 | コードは Unlicense。音楽は Future Crew のリリースに準拠 (クレジット明記) |

---

## 7. 参考リンク

- Second Reality ソース: https://github.com/mtuomi/SecondReality
- クロスツールチェーン: https://github.com/jeffv03/amiga-gcc (Bebbo toolchain フォーク) /
  Docker: `amigadev/crosstools:m68k-amigaos`
- c2p: `c2p.library` / `c2p-68k` / `c2pAmiga` (GitHub)
- libxmp: https://github.com/libxmp/libxmp
- ModArchive (音楽の標準形式版): `2nd_skav.s3m` (moduleid 212082) / `2nd_pm.s3m` (212083)
- ptplayer (4ch MOD リプレイヤ, Unlicense): https://github.com/AmigaPorts/ptplayer
- WinUAE: https://www.winuae.net/

---

## 8. 進捗ログ

| 日付 | フェーズ | 内容 |
|---|---|---|
| 2026-11-05 | 調査 | ソース・資産・音楽の全数調査、ロードマップ策定 |
| 2026-11-05 | **M0 完了** | `vscode-amiga-debug` 同梱ツールチェーン (GCC 15.1 / m68k-amiga-elf) でビルドし、同梱 WinUAE + 内蔵 AROS ROM (2017) で `Hello Amiga!` の実行を確認 (`run/winuae-hello1.png`)。起動は改造 AROS ブートフロッピー方式。KS 3.1 ROM はユーザー提供後に最終受け入れ確認 |
| 2026-11-05 | **M1 完了** | フレームワーク v0 (`src/m1/`) が動作。AGA 320x256 / 8 プレーン / 256 色 (24bit パレット)、ダブルバッファ (Copper 2 枚 + COP1LC 切替)、VBlank (exec `AddIntServer`)、マウス入力、chunky→planar c2p パイプラインを実装。3 テストシーン (チェッカー / 256 色プラズマ / 3D スターフィールド) の表示を確認 (最適化ビルド `-Ofast -flto -fwhole-program` で動作)。主要な知見はセクション 9 参照 |
| 2026-11-06 | **M2 完了** | STMIK 互換 S3M リプレイヤ (`src/m2/`) が MUSIC0 / MUSIC1 を再生 (MUSIC1 はデータ用フロッピー DF1: から 720KB をロード)。STMIK.300 を完全解析し `np_*` 互換 API を実装 (Paula 4ch / 8 ボイス 2:1 ミックス / 15,625Hz)。VBL 同期の音楽タイミング、Fast RAM ロード (HUNKF_FAST)、PAL 50Hz を確認。詳細はセクション 10 参照 |
| 2026-11-06 | 環境 | GitHub リポジトリ (private) を開設し、別 PC ワークフローを確立 (clone → ビルド → ADF 作成を検証済み)。スクリプトを環境変数対応 (`AMIGA_TOOLCHAIN` / `AMIGA_WORKDIR`) にし、セットアップ手順を `README.md` に記載 |
| 2026-11-06 | **M3 着手** | c2p 高速版 (`c2p8_fast` = 4x4 ビット転置) を追加 (自己テストで出力完全一致・178 命令/8px)。シーン API (`src/m2/parts/`) と最初のシーン「スターフィールド」(planar 直接描画・音楽同期) を実装。詳細はセクション 11 参照 |
| 2026-11-06 | **M3 デモ動作** | プラズマ (chunky+c2p)、タイトル画像 (オリジナル SRTITLE.LBM を変換)、スターフィールドの 3 シーンが音楽オーダー同期 + フェード付きで巡回する「最初の動くデモ」を確認。ILBM/PBM→IMG1 コンバータ (`tools/lbm2raw.py`) とデータ用フロッピー運用を確立 |
| 2026-11-06 | **M3 実パート移植 (1)** | DIS 互換 API (`framework/dis.c/h`) を実装。**END (エンドピクチャ)** を移植 — `_PIC.OBK` (OMF) から uh 形式画像を抽出 (`tools/uh2raw.py`)、白フラッシュ→クロスフェード→音楽待ち→フェードアウト (オリジナル END.C 準拠)。データ用フロッピーを 2 枚構成 (DF1: 音楽 / DF2: 画像) に変更。詳細はセクション 11.7 / 11.8 参照 |
| 2026-11-07 | **M3 ALKU 検証完了** | **ALKU (イントロテキスト)** を検証・修正。ハードウェア横スクロール (672px ワールド + BPLCON1/BPLxPT) で背景をパンし、テキストを画面固定で重ねる方式が動作。イントロ 3 ページ (A / Future Crew / Production → First Presented / at Assembly 93 → in + Dolby ロゴ) とクレジット 4 ページ (背景のみパン、テキスト固定) を確認。通常ビルドで 5 シーン巡回 (stars → plasma → title → END → ALKU) を確認。詳細はセクション 11.9 参照 |
| 2026-11-07 | **M4 着手** | **DOTS (MiniVectorBalls)** を移植 (実パート第 2 号)。512 個の玉の物理 (渦/落下/リング/散布/重力バウンド) と透視投影を planar 直接描画で実装。シーン巡回に追加して 6 シーンに。詳細はセクション 12.1 参照 |
| 2026-11-07 | **M4 パート追加** | **TUNNELI (ドットトンネル)** を移植 (実パート第 3 号)。101 リング × 64 点のドットトンネルを planar 直接描画で実装。sinit/cosit は式から実行時生成。シーン巡回に追加して 7 シーンに。詳細はセクション 12.2 参照 |
| 2026-11-07 | **M4 パート追加** | **SINUS (3D シヌスフィールド)** を移植 (実パート第 4 号)。24x24 グリッドの波面を透視投影 + 線分描画で planar に実装 (COMAN/3DSINFLD の再実装)。シーン巡回に追加して 8 シーンに。詳細はセクション 12.3 参照 |
| 2026-11-07 | **M4 パート追加** | **SCROLL (グリーティング縦スクロール)** を移植 (実パート第 5 号)。320x512 のリングバッファ + BPLxPT の行オフセットでハードウェア縦スクロールを実装 (video_set_custom_screen_rows / video_set_scroll_y を追加)。シーン巡回に追加して 9 シーンに。詳細はセクション 12.4 参照 |
| 2026-11-08 | **ALKU 修正 + 音声修正** | ALKU の横スクロール coarse/fine 計算を修正 (BPLCON1 の delay 方向を考慮) し、TEMP マーカーで方向を確認。フォントを 1 列 = 1px 化。右マウスボタンで次シーンへ送るデバッグ機能を追加。**Paula は AUDxLEN が 0 になると DMA を停止するため、AUDxPTR と AUDxLEN を再設定して再開するよう修正。さらに 2 フレーム (40ms) バッファにして VBL 落ちに強くした**。カスタム画面 (ALKU/SCROLL) では通常プレーンのデバッグ描画をスキップし、50fps と音声バッファを維持。通常の 9 シーン巡回を再確認 |
| 2026-11-08 | **音声の原因確定** | WinUAE の音声セッション単体を WASAPI ループバック録音し、`run/m0.uae` の明示的な `sound_output=exact` 等が無音の原因と確定。既定設定に戻して音楽が連続再生されることを確認。静的な矩形波 DMA テストでも検証 |
| 2026-11-08 | **M4 パート追加** | **WATER (RAYSCRL)** を移植 (実パート第 6 号)。TAUSTA の水面背景 + WAT1-3 の更新リスト + MIEKKA の剣カラーテーブルを planar 直接描画で実装。320x200 を 320x256 の中央 (y=28) に配置。シーン巡回に追加して 10 シーンに。詳細はセクション 12.5 参照 |
| 2026-11-08 | **M4 パート追加** | **FOREST (MNTSCRL / Vuori-Scrolli)** を移植 (実パート第 7 号)。背景/パレットは `HILLBACK.OBJ` の最終背景 (FINAL と同一) + 6bit パレット、マスクは `O.SCI` の 237 バイト詰め加算マスク、更新は POS1-3 のリストを使用。320x200 を中央配置し、シーン巡回に追加して 11 シーンに。詳細はセクション 12.6 参照 |
| 2026-11-08 | **パート順・ALKU・TUNNEL・画面位置 修正** | シーン巡回をオリジナル (`U2.ASM`) の順に修正: `alku → pam → BEGLOGO (タイトル) → tunnel → forest → plasma → dots → water → sinus → ddstars → end → scroll`。最終版で未使用の `START` (stars) を外し、タイトルをイントロの後ろへ移動。**ALKU**: テキストを画面固定 (背景だけが横スクロール) に変更し、ワールド 2 枚のダブルバッファで震えを解消。BPLCON1 のファインスクロールをやめて背景を 1px シフトコピーする方式にし、左端のカラム欠けを解消。スペース (16px) を追加して単語間を空けた。**TUNNELI**: リング色の伝播 (`ring_c`) をオリジナル通りにし、開始時は全ドット非表示 → 奥から徐々に出現するようにした。**画面位置**: DIWSTRT/DIWSTOP を `0x1481/0x14C1` にして 320x256 が WinUAE の表示領域に収まるよう上へ移動。詳細はセクション 12.10 参照 |
| 2026-11-08 | **PLZPART プラズマ移植** | オリジナル `PLZPART/PLZ.C` + `ASMYT.ASM` の `plz()` を移植。`PSINI.INC` / `LSINI4.INC` / `LSINI16.INC` を `tools/plz_prepare.py` でバイナリ化し、2 層 (k/l) を横 1 ドットおきに交互に出す式、`moveplz` のドリフト、`ptau` 由来の 5 パレット + 音楽 tick 同期を再実装。垂直は 1/2 解像度で評価して約 35fps。詳細はセクション 12.11 参照 |
| 2026-11-08 | **TUNNELI リング形状修正** | リングの点を自前の円 (半径 25〜133px) から、オリジナルの `pcalc[138][64]` (`TUNNEL.DAT`) に置き換え。手前のリングは 486x286 と画面より大きくなり、画面全体にトンネルの弧が広がる。リング中心のずれは 80px で頭打ちにして画面外へ飛ばないようにした。詳細はセクション 12.2.1 参照 |

---

## 9. M1 実装メモ (ハマりどころと確定事項)

M1 の実装で確定したハードウェア/ツールチェーンの要点。以後のパート実装でも前提とする。

### 9.1 AGA 画面設定

- **8 プレーン時の BPLCON0 は `0x0210`**。AGA ではプレーン数フィールドの最上位ビットが
  bit4 (BPU3) に移動しており、`0x8200` ではプレーン数 0 と解釈され背景色のみ表示になる。
- 320x256 PAL: DIWSTRT=`0x2C81` / DIWSTOP=`0x2CC1` / DDFSTRT=`0x38` / DDFSTOP=`0xD0`、
  プレーン 1 枚 = 40 bytes x 256 行 = 10,240 bytes。
- AROS はプログラマブルビデオモードを使うことがあるため、初期化時に
  BEAMCON0 (0x1DC) / FMODE (0x1FC) / BPLCON3 (0x106) を 0 にリセットする。
- WinUAE の AROS ROM 起動は NTSC になることがある。`ntsc=false` を設定して PAL を強制。

### 9.2 コッパーリストの鉄則 (最重要)

- **コッパーの MOVE 命令はレジスタ番地が 9bit にマスクされる** (0x000-0x1FE)。
  0x200 以降を指定した命令は 0x000-0x1FE にミラーされ、`0x280`→COP1LCH、
  `0x288`→COPJMP1、`0x294`→DMACON、`0x298`→INTENA など**致命的なレジスタを破壊する**。
  → **コッパーリストには 0x1BE 以下のレジスタしか書かない**。
  (256 色パレットを単純に 0x180+2i で書くと i>=64 でこの罠を踏む。M1 で実際に踏んだ)
- リスト末尾は `0xFFFF, 0xFFFE` (終端 wait)。その後はフレーム末までゼロ領域を
  実行するが、ゼロ = register 0 への MOVE なので実害はない (バッファをゼロクリアしておく)。
- COP1LC の切替は VBlank 直後 (VERTB 後) に行うと次フレームから有効になる。

### 9.3 AGA 256 色パレット

- **BPLCON3 (0x106): BANK = bits 15-13、LOCT = bit 9**。
  (bits 11-9 と bit13 と誤解しやすく、間違えると後続バンクの書き込みが bank 0 を
  上書きして全色 0 = 黒画面になる)
- 書き込み手順 (バンク b = 0..7、色 c = 0..31):
  1. `BPLCON3 = (b << 13)` → `COLOR(c) = 上位 12bit` (0x0RGB)
  2. `BPLCON3 = (b << 13) | 0x0200` (LOCT) → `COLOR(c) = 下位 12bit` (0x0RGB)
  3. 全バンク後 `BPLCON3 = 0` に復帰
- コッパーリストで毎フレーム 8 バンク分 (約 1,100 ワード) を書く方式で 24bit パレットを実現。
  リストはチップメモリに確保 (M1 では 2,400 bytes x 2 枚)。

### 9.4 タイミング / 割り込み

- VBlank は **exec の `AddIntServer(INTB_VERTB, ...)`** で取得する (AROS/KS3.1 両対応)。
  VBR を直接触る方式は AROS の `Supervisor()` 経由でトラップを起こすため使わない。
- **`WaitTOF()` は VERTB 割り込みが止まっていると動かない** (ハングする)。
  割り込み停止中 (TakeSystem 中など) は VPOSR (0xDFF004) のポーリングで待つ。
  VPOSR の垂直位置は V8=bit0 / V7-V0=bits15-8 で、値が減少する瞬間 (折り返し) がフレーム境界。
- GCC の最適化は **NULL ポインタ演算を未定義動作として `trap #7` に置換**することがある
  (`(u8*)NULL + 0x6C` のようなコード)。ベクタアドレスは整数演算 + volatile 経由で組む。

### 9.5 入力

- **左マウスボタン = CIA-A PRA (0xBFE001) bit6 (0 で押下)**。
  POTINP bit6 は右ボタン/環境により 0 固定のことがある (WinUAE で 0 だった)。
- 右ボタンは POTINP bit10 を使用 (環境により要確認)。

### 9.6 ビルド/検証ワークフロー (確立済み)

1. ビルド: `gnumake -j8` (ツールチェーンの `opt\bin;win32` を PATH に追加)
2. `run/make-adf.ps1 -Exe <プロジェクト>\out\<name>.exe -RunName <name>.exe` でブート ADF 更新
3. `run/run-m0.ps1` で WinUAE 起動 (AROS ROM + 改造ブートフロッピー、`ntsc=false`)
4. `run/list-windows.ps1` + `run/capture-hwnd.ps1` でスクリーンショット取得
5. GDB デバッグ: 設定に `debugging_features=gdbserver` + `debugging_trigger=<name>.exe` を追加し、
   `m68k-amiga-elf-gdb.exe -q -batch -ex 'target remote localhost:2345' ... out/<name>.elf`
   (接続は起動 40 秒後。PowerShell では `-ex` 引数をシングルクォートで囲む。
   シンボル再配置に失敗することがあるため、その場合は WinUAE を再起動して接続し直す)

---

## 10. M2 実装メモ (音楽: STMIK 互換 S3M リプレイヤ)

### 10.1 STMIK.300 の解析 (完了)

実物の `MAIN/STMIK.300` (x86 OMF オブジェクト) を OMF 解析 + 逆アセンブルし仕様を確定
(ツール: `tools/omf_stmik.py` / `omf_refs.py` / `omf_callers.py`)。

- `np_zinfo`: S3M コマンド 'Z' (26) の info バイト (コマンドハンドラテーブル [26] が `mov [0x8F7],al`)。
- `np_zplus`: ビットフィールド。bit0 = 次のオーダーが 0xFE (スキップ)、bit1 = 現在のオーダーが 0xFE を通過。
  DIS の `dis_musplus()` が「符号付き行距離」へ変換する (`player.c` に実装済み)。
- オーダーリスト: 0xFE = スキップ、0xFF = 終端 (loop_mode 2 = 停止、1 = loop_order へループ)。
- リポジトリ同梱のパターンデータは XOR 難読化 (解読済み: `orig[rel] = std[rel] XOR key(rel & 0xFF)`、
  STMIK は実行時に `(si<<2)^si` で復号)。デコード結果は ModArchive 標準版と完全一致するため、
  変換は標準版を入力に使用。

### 10.2 音楽データ変換 (`tools/s3m_convert.py`)

標準 S3M → 独自 `.srm` (ビッグエンディアン、4 バイトアライン): ヘッダ 32B
(マジック 'SRM1' / オーダー数 / 楽器数 / パターン数 / チャンネル数 / speed / tempo / mastervol / 各オフセット)、
オーダーは**バイト列**、楽器 48B、展開パターン 64 行 × 8ch × 5B、8bit signed サンプル。

- 変換結果: `music0.srm` (528,838B) / `music1.srm` (720,548B)。
- 注意: オーダーリストはバイト列 (16bit 読みするとバイトスワップする)。マジックは 'SRM1' を
  ビッグエンディアンで書く (`53 52 4D 31`)。

### 10.3 再生アーキテクチャ (`src/m2/`)

- `music/player.c`: S3M リプレイヤ本体 (8 ボイス。ノート/音量/エフェクト D/K/Q/A/Z/M/B/C/F/O/E/S/9)。
  STMIK 互換 API (`zinitmodule` / `zplaysong` / `zgotosong` / `zstopsong` / `zpollme`、
  `np_ord/np_pat/np_row/np_speed/np_tempo/np_zinfo/np_zplus/np_zframe`、`dis_*` ヘルパ) を提供。
- `music/audio.c`: 8 ボイス → Paula 4ch へ 2 ボイスずつミックス
  (ch0=L voices0,2 / ch1=R voices1,3 / ch2=L voices4,6 / ch3=R voices5,7)。
  ミックスレート 15,625Hz (1 サンプル/走査線、312 サンプル/フレーム、Paula period 227)。
  チップ RAM のダブルバッファ + DMA。
- ピリオドは MOD スケール (C-4 = 1712)。step = `c2spd * 7181 / period`。

### 10.4 音楽タイミング

- 50Hz (PAL) で 1 VBL = 1 tick。`music_frame()` は**VBL 経過数だけ `player_tick()` を呼ぶ**
  (ループが 50fps 未満でも音楽は実時間に同期する)。
- AROS の `VBlankFrequency = 50` (PAL) を実機確認済み。
- **重要 (2026-11-11): `video_init()` が BEAMCON0 に 0 を書いていたため、ECS/AGA では
  PAL ビット (0x0020) が落ちて NTSC (約 60Hz) に切り替わっていた。**
  - 音楽・アニメーションが 1.2 倍速になる。
  - オーディオのリングバッファ (2 VBL = 626 サンプル前提) が Paula DMA より
    速く進み、バッファの内容が DMA に追い越される。その結果、音楽が
    「連打のように速く鳴る / 一瞬で終わる」ように壊れる。
  - `BEAMCON0 = 0x0020` (PAL) を書くよう修正して解決。VBL 前提のコードは
    すべて 50Hz 前提なので、BEAMCON0 は常に PAL を維持すること。
  - 切り分けには「PC でレンダリングした PCM を Paula に直接流す」デバッグ
    モード (`DH0:pcmtest.cfg` + `DH0:music1_pcm.raw`, `M2_PCMTEST=1` で HDF に
    格納) が有効だった。プレイヤーを通さず同じ症状が出れば音声出力経路、
    出なければプレイヤー側と判断できる。

### 10.5 検証環境の確定事項 (重要)

- **WinUAE のメモリ設定キーは `fastmem_size` (MB)**。`fast_memory` は FS-UAE のキーで WinUAE では無効
  — M0 の設定は Fast RAM が無い状態で、全て Chip RAM にロードされ極端に遅かった (原因確定)。
- **HUNKF_FAST パッチ**: AROS の LoadSeg はハンクタイプ bit31 (HUNKF_FAST) を解釈して
  MEMF_FAST から確保する (`rom/dos/internalloadseg_aos.c`)。`tools/hunk_fast.py` で .exe の全ハンクに
  bit31 を立て、コード/データを Fast RAM にロード (Makefile に組込み)。AmigaOS の LoadSeg は
  このビットを無視するため実機でも安全。
- `chunky` バッファは `MEMF_FAST` (CPU 専用)。画面 / Copper / オーディオバッファは Chip RAM。
- CPU: `cpu_model=68030` + `cpu_speed=max` + `fastmem_size=8`。起動高速化に `floppy_speed=0` (ターボ)。
- **gdbserver の `debugging_trigger`** はプログラムのロード時にエミュレーションを停止して
  デバッガ接続を待つ。GDB 接続後は `continue` で実行が再開する (停止状態を「初期化前」と誤認しないこと)。
  ブレークポイントは絶対アドレス (`break *((unsigned int)main + offset)`) が確実
  (LTO で関数が main にインライン化されるため)。
- デバッガ接続中はエミュレーションが大幅に低速化する。絶対性能の評価はデバッガなし (JIT 有効) で行う。
- WinUAE ウィンドウは `run/move-winuae.ps1` で画面最下部へ移動できる (タイトルバーのみ表示、内容は画面外)。
  画面外でも `capture-hwnd.ps1 -PrintWindow` でキャプチャ可能。
  **環境変数 `AMIGA_WINUAE_MOVE=0` を設定すると移動しない** (通常のデスクトップで作業する PC 向け)。
  画面外でも `capture-hwnd.ps1 -PrintWindow` でキャプチャ可能。
- RDP セッション切断中は WinUAE が起動できない ("no display adapters")。`gfx_api=2` (DirectDraw) 必須。

### 10.6 M2 の検証結果 (2026-11-06)

- MUSIC0 を再生: `np_tempo=125` / `np_speed=6` で進行し、行/パターンが正しく進む。
  自己テスト (player_tick 100 回 → np_row=17 ≈ 100/6) で tick→行変換を確認。
- ミキサ出力は非ゼロ、Paula DMA 有効 (DMACONR = 0x03cf)、ボイス音量バーが動作。
- ループは VBL 同期で動作 (音楽 tick が VBL に追従。デバッガ接続時は 2 フレーム/イテレーション)。
- **既知の課題 (M3 以降)**: c2p の性能。320x256x8 の全画面変換は乗算トリック版で
  約 200 命令/8px、4x4 ビット転置版 (`c2p8_fast`) で 178 命令/8px (自己テストで出力完全一致を
  確認済み)。改善は約 1.1〜1.2 倍に留まり、68030/50MHz での全画面 50fps には依然不足する見込み。
  → 対策: プレーン数削減 (16〜64 色) / 部分更新 / asm 最適化 / ブラッタ併用。
  軽い演出 (星・ベクター・テキスト) は planar 直接描画で c2p を回避する (M2/M3 のハーネスも同様)。

### 10.7 アセット読み込み基盤 (2026-11-06)

M3 以降のパートデータ (画像/音楽/3D データ) を実行ファイルに埋め込まずに読むための基盤。

- **データ用フロッピー (DF1:) 方式**を採用:
  - `run/m0data.adf` = FFS フォーマット済み ADF (amitools の `xdftool` で作成・書き込み)。
  - 設定 (`run/m0.uae`) で `floppy1=<path>` + `nr_floppies=4` により接続。
  - `make-adf.ps1` の `-ExtraStartup "dir DF1:"` (既定) で、起動時にデータボリュームを
    マウントさせる。**AROS はフロッピーボリュームを自動マウントしないため、事前に一度
    アクセスする必要がある** (未マウントのまま Open すると挿入要求でハングする)。
- **AROS の重要な制約 (3 点)**:
  1. フロッピーのボリュームは起動時に `dir DF1:` 等でマウントしておく。
  2. **`TakeSystem()` の後は割り込みが止まっているため、フロッピー I/O はハングする。
     ファイルロードは必ず `TakeSystem()` の前に実行する。**
  3. 一括 Read (数百 KB) はハングするため、4KB ずつ読む (AROS の LoadSeg と同じ)。
- `framework/file.c` の `file_load_fast(name, expected_size, size_out)`:
  Fast RAM へロード (サイズ既知前提・Seek 不使用)。
- **検証**: MUSIC1 (`music1.srm`, 720KB) を DF1: からロードして再生確認
  (`np_tempo=130` / `np_speed=3` を画面バーで確認)。
- 注: WinUAE のディレクトリマウント (`filesystem=rw,dh0:...`) は AROS では機能しない
  (Open がハングする)。フロッピー/HDF の実デバイス経由を使うこと。

### 10.8 音声が出ない問題の修正 (2026-11-08)

- **症状**: デモは動作するが音楽が鳴らない。ミキサのボイスは active、`audio_peak()` も
  非ゼロなのに、Paula から音が出ない。
- **切り分け**:
  - Windows の WASAPI ループバックで PC 全体を録音すると音が入っていたが、
    それは他アプリの音だった。**WinUAE の音声セッションだけを残して録音すると無音**
    だった (ミュート/音量は正常、セッションは active)。
  - Paula のレジスタ書き込みを逆アセンブルで確認したところ、`AUDxLC/LEN/PER/VOL` と
    `DMACON` は正しく設定されていた。静的な矩形波バッファを DMA に直接セットしても
    無音だった。
  - **`run/m0.uae` の明示的なサウンド設定** (`sound_output=exact` / `sound_frequency` /
    `sound_channels` / `sound_volume` / `sound_stereo_separation`) を外して
    WinUAE の既定設定に戻したところ、矩形波が録音できた。**設定が原因**だった。
- **修正**:
  - `run/m0.uae` から明示的なサウンド設定を削除 (WinUAE の既定を使う)。
  - 併せて `audio_frame()` は **2 フレーム (624 サンプル = 40ms) をまとめて生成**し、
    2 VBL ごとに `AUDxPTR` / `AUDxLEN` を切り替える (VBL 落ち対策)。
  - `AUDxLC` は 16bit レジスタ 2 つなので、上位ワード→下位ワードの順で明示的に書く。
- **検証**: WinUAE の音声セッションだけを残した WASAPI ループバックで、
  10ms RMS 最小 0.016 / 平均 0.059、無音区間なしを確認。
- **注意**: ALKU/SCROLL のようにカスタム画面を使うシーンで通常プレーンの
  デバッグ描画を行うと、見えない描画でフレーム時間を消費し、音声 DMA が
  枯渇する。`video_custom_screen_active()` でデバッグ描画をスキップする。

### 10.9 音楽のテンポと音程の修正 (2026-11-08)

- **症状**: BGM がオリジナルより約 2 倍速く聞こえる。
- **原因 1 (テンポ)**: デモ全体で `music1.srm` をオーダー 0 から再生していた。
  MUSIC1 は 130 BPM / speed 3 = 17 行/秒、オリジナルの導入部 MUSIC0 は
  120 BPM / speed 6 = 8 行/秒で、ちょうど約 2 倍の差があった。
  → オリジナルと同じく **MUSIC0 (組み込み) から開始**するよう変更。
  MUSIC1 は後半パート用に `DH0:music1.srm` として残置。
- **原因 2 (音程)**: `note_period[128]` が「S3M のパック済みノート
  (octave<<4|note)」をインデックスにした形になっており、さらに値も
  4 半音ぶん重複・ずれていた。E 以降の音が約 4 半音 (長 3 度) 低く鳴っていた。
  → 標準 ProTracker の半音テーブル (C-4 = 1712) に置き換え、
  ノート発音時に `semitone = (note>>4)*12 + (note&15)` で変換するよう修正。
  アルペジオ (`period_of_semitone`) と整合する。
- **検証**: 背景色に `np_ord*64+np_row` を出して実測。
  MUSIC0 で **約 8.5 行/秒** (修正前の MUSIC1 は約 17 行/秒)。
  C-4/E-4/C-5 のピリオドが 1712/1359/856 と標準値に一致。
- **今後**: オリジナルのパート順を実装したら、MUSIC0 → MUSIC1 (order 0) →
  MUSIC1 (order 42) の切り替えと、パートごとの `zgotosong` 同期を行う。

---

## 11. M3 実装メモ (最初の「動くデモ」)

### 11.1 シーン API (`src/m2/parts/`)

```c
typedef struct {
    const char *name;
    void (*init)(void);
    int (*render)(u8 *chunky, u8 *planes, int width, int height);
} Scene;
```

- `render` は chunky に描いて 0 を返す (呼び出し側が `c2p8_fast` で変換) か、
  planes (8 プレーン) に直接描いて 1 を返す。
- 軽い演出 (星・ベクター・テキスト) は planar 直接描画 (c2p 不要)、
  ピクセル単位の効果 (プラズマ等) は chunky + c2p を使う。

### 11.2 スターフィールド (`parts/stars.c`)

- 256 個の星を透視投影で手前に飛ばす (Second Reality の定番演出)。
- planar 直接描画: 8 プレーンへピクセル単位でビット操作 (256 色)。c2p 不要。
- **ダブルバッファ対応**: 星の消去は「2 フレーム前の位置」に対して行う
  (前フレーム分だけ消すと、2 面交互の描画で星が蓄積してノイズ状になる)。
- 音楽同期: `np_row` が変わるたびにパレットを回転 (色が音楽と同期して変化)。

### 11.2b プラズマ (`parts/plasma.c`)

- 256x256 のプラズマテクスチャを事前計算し、毎フレームスクロール転送 → chunky 描画。
- 呼び出し側が `c2p8_fast` で 8 プレーンへ変換 (chunky 系シーンの実戦投入)。
- 音楽同期: `np_row` によるパレット回転。

### 11.2c シーン切り替え (音楽オーダー同期)

- `main.c` で `np_ord` を監視し、シーンを切り替え (テストとして 4 オーダーごとに
  stars / plasma を交互)。切り替え時は `video_clear_planes()` で両画面をクリア。
- 切り替えはフェード付き: フェードアウト (16 フレーム) → シーン init → フェードイン (16 フレーム)。
  `video_apply_fade(level)` が現在のパレット (`cur_pal`) に係数を掛けて反映する。
- デバッグ表示のバーは「領域を 8 プレーンすべてでクリア + プレーン 0 に描画」に変更し、
  全画面シーン (プラズマ等) の上でも白で読めるようにした。

### 11.3 c2p 高速版 (`framework/c2p.c` の `c2p8_fast`)

- 4x4 ビット転置方式: 4 ピクセルの低/高ニブルをそれぞれ転置し、
  プレーンごとの 4bit を得る。転置はバタフライ 3 段 (距離 7/14/21)。
- アルゴリズムは `tools/c2p_transpose_check.py` で参照実装と完全一致を確認。
- 実行時自己テスト (`c2p_selftest`): 起動時に両実装の出力一致と所要フレーム数を計測・表示。
- 命令数: 178/8px (旧: 約 200/8px)。改善は限定的で、全画面 50fps には asm 最適化や
  ブラッタ併用などの追加対策が必要 (セクション 10.6 参照)。

### 11.4 画像シーン (オリジナルの SRTITLE.LBM) とアセット変換

- `tools/lbm2raw.py`: ILBM/PBM → 独自 IMG1 形式 (ヘッダ 16B + 256 色パレット + プレーンメジャー)。
  - **"PBM " 形式の BODY はチャンキー (1 バイト/ピクセル)** な点に注意 (プラナーと誤解すると
    縞ノイズになる)。`--chunky` で対応。`--out-height` で縦 nearest スケール (400→256)。
  - パレットは 4bit/8bit を自動判定 (値 >15 なら 8bit としてそのまま使用)。
- `assets/images/title.raw`: SRTITLE.LBM (320x400, 8 planes, ByteRun1) → 320x256 (82,704 bytes)。
- `parts/image.c`: 画像を両画面へコピーして静的表示 (render は何もしない)。
- データ用フロッピーに music1.srm (720KB) + title.raw (82KB) を収録 (`make-data-adf.ps1`)。

### 11.5 フェードのパレット破壊バグ (修正済み)

- `video_apply_fade` が `video_set_palette` 経由で `cur_pal` を「暗くしたパレット」で上書きしていた
  → フェードの度にパレットが黒へ収束し、パレットを再設定しないシーン (画像) が真っ黒になった。
- 修正: コッパーへの書き込みを `write_palette()` に分離し、`video_apply_fade` は `cur_pal` を変更しない。
- 教訓: フェード/エフェクト系は「基準パレット」と「表示パレット」を分離して管理する。

### 11.6 検証結果 (M3)

- シーン巡回 (`np_ord` 同期・4 オーダーごと + フェード) で
  **タイトル画像 (オリジナル SRTITLE) → スターフィールド → プラズマ** が表示されることを確認。
- さらに END (エンドピクチャ) を加えた 4 シーン巡回
  (スターフィールド → プラズマ → タイトル → END) を確認 (セクション 11.8)。
- デバッグ用に `-DTEST_FIXED_SCENE=<n>` でシーン固定表示が可能 (シーン単体の確認用)。
- デバッグ表示のバーは開発用 (最終デモでは非表示にする予定)。

### 11.7 DIS 互換 API (`framework/dis.c/h`)

オリジナルの DIS (Demo Interrupt Server, V1.0) の C インターフェースを再実装。
パート側のコードをほぼそのまま使えるようにする。

| 関数 | オリジナル | 実装 |
|---|---|---|
| `dis_version()` | version_0 | 0x100 を返し状態を初期化 |
| `dis_indemo()` | indemo_3 | 1 (デモ実行中) |
| `dis_waitb()` | waitb_1 | VBL 待ち + 前回からの経過フレーム数 (フレーム落ち補正) |
| `dis_exit()` | exit_2 | 左マウスボタンで 1 |
| `dis_partstart()` | partstart | dis_version を呼ぶ |
| `dis_msgarea(n)` | msgarea_5 | 64 バイト × 4 の共用エリア |
| `dis_muscode(code)` | muscode_6 (AX) | np_zinfo |
| `dis_musplus()` | muscode_6 (DX) | np_zplus/np_row から符号付き行数 (STMIK 解析と一致) |
| `dis_musrow()` | muscode_6 (BX) | np_row |
| `dis_setcopper(n, fn)` | setcopper_7 | 帰線 (n=3) のみ VBL から呼ぶ (ラスター割り込みは未実装) |
| `dis_setmframe()` / `dis_getmframe()` | musframe_9 | np_zframe |
| `dis_sync()` | sync_10 | ordersync1 テーブル (order,row → パート番号) |

- **ordersync1** (オリジナルのパート遷移テーブル) をそのままデータとして収録
  (`{ (order<<8)|row, part }` 15 エントリ)。MUSIC0 のパート遷移に対応。
- メインループは `dis_version()` / `dis_exit()` / `dis_waitb()` を使用。
- 検証: デバッグ表示の sync バー (パート番号 ×3) で動作を確認。
- 注: オリジナルの sync_10 は境界の 1 つ手前を返す実装 (off-by-one) だが、
  本実装は「その位置で始まるパート」を返す意図した動作とした。

### 11.8 END (エンドピクチャ) の移植 — 実パート第 1 号

オリジナル: `reference/SecondReality/END/` (END.C + READP.C + `_PIC.OBK`)

- **`_PIC.OBK` (OMF) から "uh" 形式画像を抽出** (`tools/uh2raw.py`):
  - **OMF のレコード長フィールドはチェックサム 1 バイトを含む**
    (payload = length - 1、次のレコードは 3 + length 先)。ここを誤ると全データが 1 バイトずつずれる。
  - **uh ヘッダは 16bit int** (DOS の `int` = 2 バイト):
    magic(0xFCFD), wid(320), hig(400), cols(256), add(49)。
    パレットは +0x10 から cols*3 (VGA 6bit → `<<2` で 8bit 化)。
    行データは `add*16` (= 0x310) から、各行 u16 長 + RLE (0x80|n = n 回繰り返し)。
- `assets/images/endpic.raw`: 320x400 → 320x256 (82,704 bytes, 8 プレーン)。
- **`parts/end.c`** (オリジナル END.C 準拠のシーケンス):
  1. 白フラッシュ (全色白、32 フレーム保持)
  2. 白 → 画像パレットのクロスフェード (128 段階)
  3. `dis_musplus() > -16` (音楽マーカー接近) を待つ (最大 5000 フレーム)
  4. 黒へフェードアウト (64 段階)
- **データ用フロッピーを 2 枚構成に変更**: DD の実効容量 816KB に対し
  音楽 720KB + 画像 82KB × 2 は入らないため、DF1: = 音楽 / DF2: = 画像 (title/endpic)。
  - `make-data-adf.ps1` が 2 枚を生成し、**書き込み検証 (list に無ければエラー)** を実施。
  - WinUAE は `floppy2type=0` を明示しないと DF2: が有効にならない (requester が出る)。
  - `S/Startup-Sequence` は `dir DF1:` + `dir DF2:` で両方をマウント。
- 検証: `-DTEST_FIXED_SCENE=3` で白 → 画像 → 黒のシーケンス、
  通常ビルドで 4 シーン巡回 (stars → plasma → title → END) を確認。

### 11.9 ALKU (イントロテキスト) の検証と修正 (2026-11-07)

オリジナル: `reference/SecondReality/ALKU/MAIN.C` (VGA mode-X 320x200 / 4 プレーン)

- **方式**: 672x256 のワールド (背景 640px + 右端フェッチ余白) をチップ RAM に置き、
  カスタム画面として表示。横スクロールは **BPLCON1 (ファイン 0-15px) + BPLxPT (16px 単位)
  + BPL1MOD/BPL2MOD**。ファイン分の余分ワードをフェッチするため DDFSTOP を 0xD0→0xD8 に広げ、
  モジュロを `stride - 42` に調整する (これが無いと右端が最大 15px 欠ける)。
- **背景**: `hoi.uh` (640x200, 256 色) を `c2p8_fast` でプラナー化し、256 行へ縦拡大して
  `hoi_planar` (Fast RAM) に保持。9 フレームごとに 1px スクロール (オリジナルの SCRLF=9 と同じ)。
- **テキスト**: フォント `fona.uh` (1500x30, ハーフピクセル 3 階調) を 1 グリフ = 幅/2 の
  論理ピクセルとして描画。テキストは**画面固定** (オリジナル同様、背景だけがパンする)。
  スクロールのたびに「テキスト領域の背景を `hoi_planar` から復元 → 再描画」する。
- **パレット**: HOI の 6bit パレットを 8bit 化。色 253-255 をテキスト用の 3 階調グレーに
  差し替え、背景 (`pic_level`) とテキスト (`text_level`) を別々にフェードする。

#### ハマりどころ (今回確定)

- **`alku_init()` の中から `draw_overlay()` を呼ぶとハングする** (画面が黒いまま、CPU は idle)。
  `restore_box()` や `draw_text_center()` を init から直接呼ぶ分には問題ないが、
  オーバーレイ描画関数を init から呼ぶと必ず固まる (原因未特定。LTO/コード配置依存と推測)。
  → **対策: `set_overlay()` はフラグ (`need_overlay_draw`) を立てるだけにし、実際の描画は
  `alku_render()` の先頭で行う**。この方式なら init 直後でも 1 フレーム遅れで正しく描画される。
- **`set_overlay()` は NULL 行を許容する** (`lines[i] ? lines[i] : ""`)。クレジットページは
  4 行目が 0 (NULL) のため、そのままだと `text_w(NULL)` でクラッシュする。
- 背景のパン量は 320px (背景 640 - 画面 320)。ワールドは 672px あるので右端に余白が出ない。
- Dolby ロゴはフォントの一部 (列 1014-1228) を直接描画する。フォントデータは
  「塗りつぶしブロック + 文字の穴」のネガ形式なので、そのまま描くとブロック状になる
  (現状はオリジナルの見た目を再現できていない。要調整)。

#### 検証結果

- `-DTEST_FIXED_SCENE=4` (ALKU 固定) でイントロ 3 ページ + クレジット 4 ページを確認。
- 通常ビルドで 5 シーン巡回 (stars → plasma → title → END → ALKU) を確認。
- シーン切り替え時は `video_set_custom_screen(0, 0)` でハードウェアスクロールを解除してから
  `video_clear_planes()` する (解除しないと次のシーンがカスタム画面のまま表示されない)。

#### 2026-11-08 修正 (横スクロール方向とフォント)

- **BPLCON1 の delay は画像を右へずらす**。左スクロール (表示開始位置を右へ進める) には
  `coarse = ceil(x/16)`、`fine = (16 - (x & 15)) & 15` とし、
  実効オフセット = `coarse*16 - fine = x` になるよう修正した。
- フォントは **1 列 = 画面 1 ピクセル** で描画する (従来は 2 列を加算して 1 論理ピクセル)。
- TEMP マーカー (ワールド座標 x=300 の縦線) を置いて検証:
  スクロールが進むとマーカーは画面左へ移動し、テキストは画面中央に固定されたまま。
  これで背景だけがパンするオリジナルの見た目になった。
- カスタム画面 (ALKU/SCROLL) では通常プレーンのデバッグ描画をスキップし、
  ALKU の FPS を 37.8 → 59 (WinUAE 表示) に改善。音声 DMA の枯渇も解消。
- 右マウスボタンで次のシーンへ送るデバッグ機能を追加 (`input_next_pressed()`)。
  起動時に押しっぱなしと誤検出しないよう、`input_init()` で現在のボタン状態を初期化する。

---

## 12. M4 実装メモ (中規模パート)

### 12.1 DOTS (MiniVectorBalls) — 実パート第 2 号 (2026-11-07)

オリジナル: `reference/SecondReality/DOTS/` (MAIN.C + ASM.ASM, 512 個の玉)

- **移植方針**: 元は Mode 13h (320x200 chunky) だが、全画面 c2p は 50Hz に載らないため
  **planar 直接描画**とした。玉は 3x3 ピクセル + 中心を 1 段明るくする。
- **玉の色 (2026-11-08 修正)**: 元 `MAIN.C` の `cols[]` と `ASM.ASM` の
  `depthtable1/2/3` をそのまま使う。単一の青緑系の色相で、
  `a = depth>>8; c = 15 - clamp((a-31)*3/4+8, 0, 15)` により
  奥行きが深いほど暗くなる。外周 = `2+4c`、中心 = `3+4c`
  (パレット 128..191 に格納)。以前の「4 色 (白/青/橙/シアン) を
  玉ごとに割り当てる」実装は誤りだった。
- **影 (2026-11-08 追加)**: オリジナル `ASM.ASM` の shadow 部分を移植。
  玉とは別に、床 (y=8192 固定) へ投影した位置
  `shy = 128 + 8192*82/depth` に 2 ドットの暗いグレーを打つ。
  元はパレット 87 (背景ランプ 23 段目) の固定色。こちらは背景ランプが
  圧縮されているため、背景の 8 段目 (パレット 16) の固定色にした。
- **背景**: オリジナル `MAIN.C` のグレーランプ (無彩色、最大 6bit 15 = 8bit 60)。
  以前は青みを足した明るいグラデーションになっていたのを修正。
  毎フレーム**画面全体**を塗り直す (上半分も黒でクリア) ことで
  前フレームの玉も消える。上半分を塗っていなかったため玉の軌跡が
  残っていたのも修正 (2026-11-08)。
- **物理**: 元の 5 フェーズをそのまま移植。
  0-499 渦 / 500-899 落下 / 900-1699 リング / 1700-2359 ランダム散布 + 重力 /
  2360-2439 パレット明転 / 2440- 白フェード。
  重力と床バウンド (`yadd += grav; y += yadd; y >= 8105 で反発`) も元の ASM 準拠。
- **投影**: Y 軸回転 + 透視投影。元の 16.16 固定小数点 (`imul` + `shrd`) を C の
  整数演算に置き換え (`depth = (z*cos - x*sin)>>16 + 9000`, `sx = 160 + (v*1.125)/depth`,
  `sy = 128 + y*82/depth`)。256 行に合わせて中心を 100→128、y スケールを 64→82 に拡大。
- **sin テーブル**: 元の `SIN1024.INC` (振幅 256) を Bhaskara の近似式で実行時生成
  (`sin(πt/512) ≒ 16u/(5-4u), u = t(512-t)/512²`)。テーブルを同梱しない。
- **注意**: 元のパートは 2450 フレーム (約 49 秒) の長い演出。現在のシーン切り替え
  (4 オーダーごと) では最初の 1〜2 フェーズしか見えない。通しでの確認は M6 で行う。
- **現状**: 動作確認済み (5 シーン巡回に追加、6 シーンに)。玉の色は
  オリジナル準拠に修正済み。大きさ・散布範囲の微調整は今後。

### 12.2 TUNNELI (ドットトンネル) — 実パート第 3 号 (2026-11-07)

オリジナル: `reference/SecondReality/TUNNELI/TUN10.PAS` + `TUNNELI.OBJ` (Turbo Pascal + asm)

- **構造**: 101 個のリング (`putki[]`) がトンネルの中心線。毎フレーム先頭リングを
  `sinit`/`cosit` の合成で動かし、全体を 1 つずつ手前へずらす。
- **描画**: リング x=80..4 の 64 点をドットで打つ。点の座標はオリジナルの
  テーブル `pcalc[138][64]` (`TUNNELI/TUNNEL.DAT`, 138*64*4 = 35,328 バイト) を
  そのまま使う。`pcalc` は 320x200 画面の中心 (160,100) を基準にした楕円で、
  半径インデックス `br = sade[x] = 16384/(x*7+95)` (25〜133) で引く。
  手前のリング (br=133) は 486x286 で画面より大きく、縁の弧だけが見える。
  前フレームの点を色 0 で消す。
- **planar 直接描画**: 元は Mode 13h のドット描画。Amiga では 8 プレーンへ直接プロット
  (約 4,900 点/フレーム × 8 プレーン)。c2p 不要。
- **テーブル生成**: 元の `SINIT.OBJ` の内容を式から実行時生成。
  `sinit[x] = sin(x/128*π) * (x*3/16)`, `cosit[x] = cos(x/256*π) * (x*4/32)`。
  sin は Bhaskara 近似 (振幅 256) で 1024 エントリ生成。
- **ハマりどころ**:
  - リングの色 `col = putki[x].c + x/1.3` は、初期状態で `c=0` だと色 3〜61 になり
    パレット上は黒。→ `c` が未伝播のときは 64 とみなすフォールバックを入れた。
  - 元のパスは時間とともに振幅が増え (`sin` の振幅が frame に比例)、数百フレームで
    リング同士の間隔が開きすぎて画面外へ飛ぶ。→ 形は保ったまま、リング中心の
    ずれの最大値が 80px を超えないよう全体を縮小する (`TUN_MAXOFF`)。
- **現状**: 動作確認済み (7 シーン巡回に追加)。中心の動き・奥行き感の調整は今後。

#### 12.2.1 リング形状を pcalc に修正 (2026-11-08)

- 以前は `sade[x]` をそのまま円の半径 (25〜133px) として描いていたため、
  リングが 320x256 画面に収まってしまい「画面全体を埋めるトンネル」に
  ならなかった。
- オリジナルは `pcalc[br][i]` を使う。`tools/tunnel_prepare.py` で
  `TUNNEL.DAT` を **ビッグエンディアンの int16** に変換して
  `assets/tunnel/pcalc.bin` にし、実行時に `DH0:pcalc.bin` からロードする。
- `pcalc` の y は 200 行画面の中心 (100) 基準なので、256 行画面では
  `+28` して中心 128 に合わせる (`PCALC_YOFF`)。
- ずれの上限 `TUN_MAXOFF = 80` を入れて、時間が経ってもリングが画面外へ
  飛ばないようにした。
- 検証 (WinUAE, `TEST_FIXED_SCENE=3`): 20 秒以上にわたって 1 フレームあたり
  約 4,000 ドットが画面の 32 行 × 40 列すべてに分布 (画面全体を覆う)。
  以前は最大半径 133px の同心円で画面中央に収まっていた。

### 12.3 SINUS (3D シヌスフィールド) — 移植パート 4 番 (2026-11-07 / 2026-11-12 作り直し)

原典: `reference/SecondReality/COMAN/`（`3DSINFLD.EXE` のソース。`GRID/` は原型）

- **経緯**: 初版 (2026-11-07) は「アルゴリズムだけ C で再実装」した簡易版で、
  波の形・回転・色がオリジナルと一致していなかった。
  2026-11-12 に **原本の 386 アセンブリを 1:1 で移植し直した**。
- **レイマーチ**: `COMAN/THELOOP.INC`（128 段のアンロール、`bx_add` は
  240→224→256→… の距離カーブ、`eax_inc` は「上位16bit=小数 / 下位=高さ増分」）。
  `tools/theloop_gen.py` が命令ごとに C へ変換し、
  `src/m2/parts/theloop_gen.c`（約 5,200 行）を生成する。
- **重要な落とし穴**: `cmp ax,bx` は CF（符号なしボロー）と SF/OF（符号付き）の
  両方を立てる。`jge` は SF==OF、直後の `adc` は CF を使う。
  この 2 つを混同すると列ごとにレイが暴れ、**2D の棒グラフ**のように見える。
  修正後、参照実装と一致しないピクセルは 15,373 → 1,506 に減少した。
- **ドライバ**: `COMAN/MAIN.C` の `doit()` を移植。`rot/rot2` の回転、
  `xwav/ywav` の波スクロール、160 列 × 200 行のフレームバッファ
  （1 列 = 画面 2px、列 0-79 が偶数側・80-159 が奇数側）、
  パレット計算も原本のまま。`W1DTA.BIN` / `W2DTA.BIN` を資産化。
- **表示**: `docopy()` と同じく vbuf 60 行目 → 画面 70 行目、
  `140 - startrise` 行を転送。`startrise` のせり上がりも実装。
- **PAL 対応**: 70Hz の 1 フレームを 1.4/VBL で進める（AGENTS.md のルール）。
- **68k アセンブラ版**: `src/m2/parts/sinus_theloop.asm` は
  `tools/kblocks.h` のブロック表（128 個）を辿る実装。
  68k の X フラグを x86 の CF に対応させ、`addx` でキャリー連鎖を再現する。
  `sinus.c` の `USE_ASM_THELOOP` で C 版と切り替え可能。
  実測では **C 版と速度は同じ**（描画 6.9 steps/capture で一致）。
  レイマーチを完全に外しても 13.3 steps/capture なので、
  **このパートのボトルネックは c2p（320x256 chunky→planar）**であり、
  レイマーチ側の最適化では効果が出ない。
- **検証**: `tools/theloop_ref.c` に SecondReality++ の `theloop3()` を
  そのまま置き、`tools/theloop_compare.py` でブロック表を突き合わせる。
  ブロック表は完全一致。描画結果の統計（隣接列の色差）も一致する。

### 12.3b U2E (Vector Part II - KewlComplex) — 2026-11-12

原典: `reference/SecondReality/VISU/C/U2E.C`（プレイヤ）＋ `MAIN/DATA/U2E.00M`,
`U2E.001`..`U2E.042`, `U2E.0AB`。描画は U2A と同じ VISU ライブラリ
（ACALC.ASM / ADRAW.ASM）なので、`u2a.c` のエンジンをそのまま流用した。

- **データ**: `tools/u2e_prepare.py` で変換。
  - ユニーク 42 オブジェクト（建物・木・ロゴ・車など、三角形 約 2,000）
  - インスタンス 58（`.00M` offset 784 の 16bit 表。先頭 co[0] はカメラで、
    `for(c=1;c<d;c++)` なので **d-1 個**しか使わない点に注意）
  - シーンスクリプト `U2E.0AB` は 1,801 フレーム / 4,045 コマンド
- **プレイヤ**: `U2E.C` のコマンド解釈を移植。
  - `0xFF fov` = フレーム区切り、`0xFF 0xFF` = シーン終端
  - `(a&0xC0)`: 0x80=表示ON / 0x40=OFF、`(a&0x30)`: pflag のバイト数
  - pflag の 2bit ずつが x/y/z のデルタ長、bit7..15 が行列要素の更新
- **重要な落とし穴**: `lsget()` は **`f&3 == 3` のとき 4 バイト読む**
  （最後のバイトを符号拡張）。3 バイトと誤るとスクリプト全体がずれ、
  存在しないオブジェクト番号（例 977）を読んで即 return し、
  カメラ行列が 0 のまま → 画面が真っ黒になる。
  `u2a.c` 側も同じ実装だったため同時に修正した。
- **表示**: 320x200 を 28 行目から（U2A と同じ）。背景は黒。
- **PAL 対応**: 70Hz の 1 フレームを 1.4/VBL で進める。
- **シーン番号**: 18（末尾に追加）。`JPLOGO -> U2E -> ENDLOGO` の流れになり、
  `hidden.cfg` があるときだけ `JPLOGO -> DDSTARS -> ENDLOGO`。

### 12.4 SCROLL (グリーティング縦スクロール) — 実パート第 5 号 (2026-11-07)

オリジナル: `reference/SecondReality/ENDSCRL/MAIN.C` (640x400 / 4 プレーン / CRTC スクロール)

- **方式**: 320x512 の 8 プレーンバッファ (チップ RAM) をリングとして使い、
  表示開始行を **BPLxPT の行オフセットだけ**で動かす (コピーなし)。
  256 行進んだら後半を前半へコピーして巻き戻す (80KB / 256 フレームに 1 回)。
- **フレームワーク拡張**: `video_set_custom_screen_rows(planes, stride, rows)` と
  `video_set_scroll_y(y)` を追加 (プレーン高さ 256 超 + 縦スクロール)。
  ALKU の横スクロールと同じ仕組みで、`set_bitplane_slot()` に行オフセットを追加した。
- **テキスト**: ALKU と同じ FONA フォント (DF2:fona.uh) を 1:1 で描画。
  1 フレーム 1 行スクロール、18 フレームごとに次の行を画面下端に描く。
- **注意**: 描画位置が 512 行の末尾をまたぐため、行を 2 回 (y と y-512) 描いて
  範囲外は `put_px` のクリップに任せている。
- **現状**: 動作確認済み (9 シーン巡回に追加)。テキストはオリジナルの
  グリーティングではなく、移植プロジェクト用の短いメッセージに差し替えた。

### 12.5 WATER (RAYSCRL) — 実パート第 6 号 (2026-11-08)

オリジナル: `reference/SecondReality/WATER/` (DEMO.PAS + ROUTINES.ASM)

- **元の方式**: VGA 320x200 (Mode 13h)。TAUSTA の水面背景に、
  - `WAT1-3.DAT`: 34 レイヤ × 158 色の更新ピクセル位置リスト (u16 LE)
  - `MIEKKA.SCI`: パレット (+10) と 400x35 の剣カラーテーブル (+778)
  を重ねる。`putrouts1` は各リスト位置について
  `color = fbuf[entry] ? fbuf[entry] : background[pos]` を書き込む。
  `fbuf` は 158x34 で、3 フレームごとに 1 列スクロールして
  フォント列を追加する。剣の形は WAT リストが画面位置を決め、
  フォントが色を与えるため、水面に剣が映り込んで動くように見える。
- **Amiga 版**:
  - 320x256 の 8 プレーンをカスタム画面としてチップ RAM に保持し、
    320x200 の背景を y=28 に配置 (上下は黒)。
  - WAT リストは `tools/water_prepare.py` でビッグエンディアン化。
  - `fbuf` を 3 フレームごとに行単位で左スクロールし、フォント列を右端へ追加。
  - リスト位置のみ planar に直接書き換える (全画面コピーなし)。
- **アセット**: `assets/water/water_bg.bin` / `water_pal.bin` / `water_font.bin` /
  `water1-3.bin`。`run/make-hdf.ps1` が HDF へ格納する。
- **検証**: `-DTEST_FIXED_SCENE=9` (WATER 固定) で水面と剣の反射を確認。
  Python シミュレーション (`tools/water_sim.py`) と見た目が一致。
  FPS 59 / SND 0% 前後で動作。
- **現状**: 通常巡回に追加して 10 シーンに。背景は 320x200 のまま中央配置
  (256 行への拡大はしていない)。

### 12.6 FOREST (MNTSCRL / Vuori-Scrolli) — 実パート第 7 号 (2026-11-08)

オリジナル: `reference/SecondReality/FOREST/` (READ.PAS + ROUTINES.ASM)

- **元の方式**: VGA 320x200 (Mode 13h)。HILLBACK の山/森の背景に、
  - `POS1-3.DAT`: 31 レイヤ × 237 色の更新ピクセル位置リスト (u16 LE)
  - `O.SCI`: 320x31 の加算マスク (先頭 237 列)
  を重ねる。`putrouts` は各リスト位置に
  `screen[pos] = background[pos] + font[bx]` を書く。
  `font` は **237 バイト詰め** (行ストライド 237) で、`scp` バイト分
  スクロールする (3 フレームごとに +1)。
- **重要な修正 (色)**: 背景とパレットは `HILLBACK.LBM` (緑の素材画像) ではなく、
  `HILLBACK.OBJ` の OMF セグメント (+10 に 768B の 6bit パレット、+778 に
  320x200 の最終背景) を使う。最終背景のピクセル列は `FINAL.LBM` と完全一致し、
  6bit パレットを `<<2` すると青/シアンの山と緑の森になる。
  マスクも `O.SCI` の先頭 237 列を 237 バイト詰めで格納する
  (320 バイトストライドにすると色/位置がずれる)。
- **Amiga 版**:
  - 320x256 のカスタム画面に 320x200 を中央配置 (y=28)。
  - POS リストは `tools/forest_prepare.py` でビッグエンディアン化。
  - 加算マスクは `(background + mask) & 0xFF` で planar に直接書き込み。
  - リスト位置のみ更新する (全画面コピーなし)。
- **アセット**: `assets/forest/forest_bg.bin` / `forest_pal.bin` /
  `forest_mask.bin` / `forest1-3.bin`。`run/make-hdf.ps1` が HDF へ格納する。
- **検証**: `-DTEST_FIXED_SCENE=10` (FOREST 固定) で表示を確認。
  画面平均 RGB 41.3/54.1/71.2 (シミュレーション 36.9/53.8/71.9、
  FINAL 36.9/55.9/74.7) で色が一致。3 秒間で 37,258 ピクセルが変化し、
  マスクのスクロールアニメーションを確認。
- **現状**: 通常巡回に追加して 11 シーンに。

### 12.7 DDSTARS (隠しパート) — 実パート第 8 号 (2026-11-08)

オリジナル: `reference/SecondReality/DDSTARS/STARS.ASM`

- **元の方式**: 1024 個の 3D 星 (x,y = -512..511, z = 0..255) を
  透視投影して 2 プレーン (4 色) に OR 蓄積し、途中から
  `TEXTS.LBM` のテキストを横方向に開く。
- **Amiga 版** (`src/m2/parts/ddstars.c`):
  - 320x256 のカスタム画面。プレーン 0/1 = 星 (色 1-3)、
    プレーン 2 = テキスト (色 4 = 白)。
  - 星は最初 512 個、6 秒 (frame 300) から 1024 個。
    前回位置を暗い色で描いて簡易軌跡にする。
  - 投影の除算を Q16 逆数テーブル `recip[256]` に置換。
  - `wput` はプレーン 0/1 のみ更新するので、星がテキストを消さない。
  - テキストは 12 秒 (frame 600, 50fps 基準) から 1 フレーム 4 列ずつ
    横に開き、80 フレームで全 320 列が開く。一度描いたら保持。
- **アセット**: `assets/ddstars/text.raw` (`TEXTS.LBM` を
  `tools/lbm2raw.py --chunky` で 1bit マスク化、IMG1 320x200x8)。
  `run/make-hdf.ps1` が HDF の `DH0:ddstars_text.raw` へ格納する。
- **検証** (`-DTEST_FIXED_SCENE=11`):
  - テキスト全開時の白ピクセル約 80,000、星の色
    (100,124,152) / (180,232,255) / 白 を確認。
  - 1 秒間で約 4,500 ピクセルが変化 (星の移動)。
  - **50Hz 同期**: アプリのフレーム数と VBL 数の差 (lag) が 5 秒間
    0 のまま = 1 VBL に 1 フレーム = PAL 50fps で動作。
- **性能問題の修正** (このパートで顕在化):
  - `video_flip()` が毎フレーム `vblank_wait()` していたのを削除。
    メインループ先頭の `dis_waitb()` と合わせて 2 VBL/フレーム
    (最大 30fps) に制限されていた。
  - `music_frame()` の VBL キャッチアップに上限 8 を設定し、
    `last_vbl` をメインループ開始前に初期化。起動中の VBL を
    一気に処理して「遅れ → 大量処理 → さらに遅れ」の
    スパイラルに陥るのを防ぐ。
  - WinUAE 側は `cpu_compatible=false` / `cpu_cycle_exact=false`
    でないと 68030 が極端に遅くなる。
  - WinUAE は既定でホストのリフレッシュ (60Hz) に同期して
    1.2 倍速で走るため、`gfx_vsync=true` + `gfx_refreshrate=50`
    で PAL 50Hz に固定する (`run/m0.uae`)。
- **現状**: 通常巡回に追加して 12 シーンに。

### 12.8 全 12 シーン巡回の確認 (50Hz) (2026-11-08)

50Hz 化のあと、全シーンが正しい順序で切り替わり、クラッシュしないことを確認した。

- 検証用に一時的な `-DSCENE_WALK` を追加:
  - 100 フレームごとに次のシーンへ自動で進む
  - 背景色 (COLOR00) をシーン番号のグレー `(n,n,n)` にして、
    キャプチャから表示中のシーンを読み取れるようにする
- マウスやフォーカスは一切操作しない (右クリック検証は
  `SetForegroundWindow` でフォーカスを奪うため廃止)。
- **結果**: 0 stars → 1 plasma → 2 image → 3 end → 4 alku → 5 sinus →
  6 tunnel → 7 dots → 8 scroll → 9 water → 10 forest → 11 ddstars → 0
  の順で巡回。各シーンの表示は約 2.6 秒 (100 フレーム + フェード 32 フレーム)
  で、50Hz の想定どおり。全シーンで描画が継続し、ハング・クラッシュなし。
- 検証後、`SCENE_WALK` のコードとビルドフラグは削除済み。

### 12.9 PAM (PRAX4.FLI アニメーション) — 実パート第 9 号 (2026-11-08)

オリジナル: `reference/SecondReality/PAM/` (VFLI.C / ANIM.C / OUTTAA.C)

- **元の方式**: `PRAX4.FLI` (320x200, 41 フレームの FLIC) を VFLI.C で
  生フレームに展開し、ANIM.C が直前フレームとの差分をパックした
  `OUT.ANI` (324,625 バイト, 42 フレーム) を再生する。
  1 フレーム 4 VBL (70Hz) で進め、`wfade` テーブルで
  フェードアウト → 黒 → フェードインする (45 ステップ)。
- **差分パック形式** (`ANIM.C` の `packframe`):
  - `0x01..0x7F` + 1 バイト: その回数だけ同じ色を書く
  - `0x80..0xFF`: `256 - 値` ピクセル読み飛ばす
  - `0x00`: フレーム終端。各フレームは 16 バイト境界から始まる。
- **Amiga 版** (`src/m2/parts/pam.c`):
  - 320x256 のカスタム画面に 320x200 を y=28 で中央配置。
  - 差分を 64,000 バイトの作業バッファへ展開し、chunky へコピーして
    `c2p8_fast` でプレーン化する。
  - 50Hz に合わせて 1 ステップ 3 VBL (60ms)。45 ステップで
    135 VBL ≒ 2.7 秒でループする。
  - パレットは 6bit → 8bit (×4) に変換し、`wfade` で減光する。
- **アセット**: `tools/pam_prepare.py` が `assets/pam/pam.ani`
  (OUT.ANI そのまま) と `assets/pam/pam.pal` (768 バイト, 8bit) を作る。
  `run/make-hdf.ps1` が HDF へ格納する。
- **検証** (`-DTEST_FIXED_SCENE=12`):
  - 最大輝度時のアニメ領域の平均 RGB は約 (10, 11, 22)。
    Python で展開した参照値 (frame 0: 7.3/7.7/18.5,
    frame 41: 12.1/13.3/26.7) と一致する範囲。
  - アニメ領域は画面 y=28..227 に収まり、上下は黒。
  - 0.4 秒間で約 19 万ピクセルが変化し、アニメーションを確認。
- **注意**: `wfade` は 45 ステップ中 23 ステップが 0 (黒) で、
  元パートは「暗転を挟むトランジション」である。そのまま再現している。
- **現状**: 通常巡回に追加して 13 シーンに。

### 12.10 パート順・ALKU テキスト・トンネル・画面位置の修正 (2026-11-08)

- **パート順** (`parts/scenes.c`, `main.c`):
  - オリジナル `MAIN/U2.ASM` の実行順は
    `START → ALKU → U2A → PAM → BEGLOGO → GLENZ → TUNNELI → ...`。
    `START` (S.EXE) は最終版では使われていない試作なので巡回から外した。
  - 巡回順を `alku → pam → image (BEGLOGO) → tunnel → forest → plasma →
    dots → water → sinus → ddstars → end → scroll` に修正 (タイトルは
    イントロの後)。
  - `scene_cues` は `0,9,11,13,20,28,36,44,52,58,62,66`。オリジナルでは
    ALKU が order 0-8 を使い、最初のテキストが order 2 で出る。
    U2A (スペースバトル) と GLENZ は未移植なので、その区間を
    PAM / タイトルに割り当てている。
  - **未移植**: U2A (VISU, スペースバトル) と GLENZ (透明ポリゴン)。
    入るべき位置は ALKU の後 (U2A) とタイトルの後 (GLENZ)。
- **ALKU テキスト** (`parts/alku.c`):
  - オリジナルの `fmaketext/maketext` はテキストを**画面固定**にし、
    背景だけを横スクロールさせる。ワールド座標に固定すると文字が背景と
    一緒に流れてしまうため、スクロール位置に合わせて
    `scroll_pos + 画面 x` へ描き直す方式に戻した。
  - 表示中のバッファを書き換えると文字が震えるので、ワールドを 2 枚
    (`world[0]/world[1]`, 8 x 40 x 256 = 80KB each) 用意し、裏側に
    「背景 + テキスト」を描いてから `video_switch_custom_screen()` で
    表示を切り替える (オリジナルも隠しページへ描いて切り替えている)。
  - **左端のカラム欠け**: BPLCON1 のファインスクロールは左端に最大
    15px の未定義帯を作る (COLOR00 でごまかしても内容が欠ける)。
    背景をチップ RAM のワールドへ 1px 単位でシフトコピーする方式に
    変え、BPLCON1 = 0 で表示する。コピーは 9 フレームに 1 回
    (80KB, 32bit シフト) で、スクロール速度はオリジナルと同じ
    1px/9 フレーム (実測 2 秒で 11px)。
  - **スペース**: フォント (FONA) にスペースが無いため、オリジナル
    (`fonap[32]=1500-20`, `fonaw[32]=16`) と同じ 16px を割り当てた。
    これで "A Future Crew Production" などの単語間が空く。
  - 検証: 2 秒間隔のキャプチャでテキストの画面座標が不変 (画面固定)、
    背景のみ 11px 移動。左端 30 列の参照画像との差は右端と同じ
    (欠け帯なし)。通常ビルドの先頭が ALKU のテキストのみになっている
    ことも確認 (画面平均 4.1, 明部 7,642px)。
- **TUNNELI** (`parts/tunnel.c`):
  - オリジナルは `bbc = putki[x].c + round(x/1.3)` が 64 未満のリングを
    描かない。`putki[99].c` に 64/128 を入れ、リングのシフトで手前へ
    伝播させることで、開始時は全ドット非表示 → 奥から徐々に出現する。
  - 修正前は `ring_c` を無視して常に 64 以上で描いていたため、開始時から
    全ドットが見えていた。修正後はフレーム 1-19 が 0 ドット、フレーム 96
    で 77 リング (4,928 ドット) に達する (Python シミュレーションで確認)。
- **画面位置** (`framework/video.c`):
  - DIWSTRT/DIWSTOP を `0x2C81/0x2CC1` → `0x1481/0x14C1` に変更。
    320x256 の画面が WinUAE の表示領域 (PAL の上下オーバースキャン込み)
    に収まり、タイトル/背景が下に寄って見えなくなる問題を解消。
    `video_switch_custom_screen()` を追加し、バッファ切替と
    スクロール位置を 1 回のレジスタ更新で行う。
- **PLZPART (プラズマ)**: オリジナルの `plz()` を移植した (セクション 12.11)。
  `vect()` (回転テクスチャキューブ) はまだ未移植。

### 12.11 PLZPART の plz() (フルスクリーン・プラズマ) 移植 (2026-11-08)

- **テーブル**: `PLZPART/PSINI.INC` (db 16384) / `LSINI4.INC` /
  `LSINI16.INC` (dw 8192) を `tools/plz_prepare.py` で
  `assets/plasma/psini.bin` / `lsini4.bin` / `lsini16.bin` に変換。
  Amiga はビッグエンディアンなので 16bit テーブルは BE で書き出す。
- **式** (`ASMYT.ASM` の `plzline` を再実装):
  - `L16 = lsini16[(c2 + y - 4g + 320) & 4095]`
  - `A   = psini[(c1 + 32g + L16) & 4095]`
  - `L4  = lsini4[(c4 + 16g + y) & 4095]`
  - `B   = psini[(c3 + 2y + L4 - 4g + 320) & 4095]`
  - `v   = (A + B) & 255`
  - 1 ドットあたりのインデックス送りは `psini +8` / `lsini4 +4` /
    `lsini16 -1` / `psini(B) -1`。
  - オリジナルは同じ走査線を k パラメータと l パラメータの 2 回に
    分けて描き、VGA の 4 プレーン (2bit x 4) へ振り分ける。
    オリジナルは同じ走査線を k と l の 2 回に分けて描き、VGA の
    4 プレーン (2bit x 4) へ振り分ける。2 つの値
    `K = A(k) + B(k)` と `L = A(l) + B(l)` をビット単位で混ぜると
    (`(K & 0xCC) | (L & 0x33)`)、K と L が大きく違う位置で
    インデックスが最大 60 以上飛び、色が別の段階へ「オーバーフロー」
    して見える。オリジナルの画面は隣接ピクセル差が非常に小さい
    滑らかな 1 次元の値なので、この移植では 2 レイヤーの平均
    `(K + L) / 2` を使う。k と l の両方の動きが入り、色は滑らかに
    変化する。
- **パラメータのドリフト** (`COPPER.ASM` の `moveplz`): 毎 VBL
  `k1-=3 k2-=2 k3+=1 k4+=2` / `l1-=1 l2-=2 l3+=2 l4+=3` (mod 4096)。
- **パレット**: `PLZ.C` の `ptau[]` から生成する。ソースには 5 種類
  (`pals[0..4]`) 定義されているが、実際のデモで表示されるのは
  `pals[0]` RGB (赤青系) → `pals[1]` RB-black (レインボー系) →
  `pals[2]` white (モノトーン) の 3 段階 (切り替え 2 回)。
  `pals[3]` / `pals[4]` はパート終了で表示されない。
  音楽 tick 723 / 1491 で切り替える。
  tick は `vblank_frames()` で数えるので、描画が 50fps 未満でも
  実時間で進む。
- **解像度と画面の帯**: オリジナルは 480 ラインの VGA モードで、プラズマは
  そのうち 280 行だけを描く (画面の上下に黒い帯が残る)。
  256 行の画面では 280/480 の高さ (150 行) の帯に収め、
  `y = (sy - 53) * 480 / 256` で走査線あたりの模様の進みを合わせる。
  これをしないと模様が縦に間延びする。
- **検証**:
  - 8 秒時点: 帯の平均 RGB (63, 0, 145) = 赤/青 (pals[0]、緑なし)。
  - 20 秒時点: (57, 51, 142) で緑が出る = pals[1] (レインボー系)。
  - 36 秒時点: (52, 52, 52) で R=G=B = pals[2] (モノトーン)。
    3 段階・切り替え 2 回で終わることを確認。
  - 1 画面内の値の並びが滑らかに変化することを Python シミュレーションと
    突き合わせて確認 (テーブルのエンディアン違いによるノイズを修正済み)。

#### 12.11.1 プラズマの不具合修正 (2026-11-08)

- **ドットが縦に伸びる**: 原因は 2 つあった。
  1. `PLZ_VSTEP = 2` で 1 行おきに計算して同じ行を 2 回書いていた
     (2 行分のブロック)。`PLZ_VSTEP = 1` にして垂直も 1:1 にした。
  2. 水平方向のインデックス送りが `psini +40` / `lsini4 +48` と
     オリジナルの `+32` / `+16` より大きかったため、模様が横に
     詰まり、縦長に見えていた。1 ドットあたり `+8` / `+4` に修正。
  さらに k と l を「横 1 ドットおきの別ドット」として描いていたのも
  誤り。オリジナルは 1 ドットの色ビットを k と l で分担するので、
  `(k & 0xCC) | (l & 0x33)` で 1 ドットに合成するようにした
  (プレーン 1,3 = ビット 2,3,6,7 / プレーン 0,2 = ビット 0,1,4,5)。
- **ドットが 2 ドット単位のブロックになる**: 途中で 2 ドット単位に
  間引いていたため。1 ドットずつ計算するように戻した。
- **色の揺らめく模様がオリジナルと違う / レイヤーが 1 つ足りない**:
  途中で l パラメータを捨てて 1 ドット = `A + B` (k のみ) にしたため、
  模様が単純になっていた。k と l の 2 レイヤーを
  `(K & 0xCC) | (L & 0x33)` で合成する方式に戻した。
- **縦 16 ドットの横縞が出る**: 偶数/奇数ラインで k/l の上下ビットを
  入れ替える処理を、圧縮前のライン番号 `y` のパリティで行っていた。
  `y = (by * 15) >> 3` は 8 画面行ごとにしかパリティが変わらないため、
  8 行周期で色の割り当てが切り替わり横縞になっていた。入れ替えを
  やめて固定の割り当てにした。
- **色が滑らかに変化せず、別の色へオーバーフローする**:
  `(K & 0xCC) | (L & 0x33)` は K と L が大きく違うとインデックスが
  最大 60 以上飛ぶ (例: 76 → 127)。オリジナルのスクリーンショット
  (Demozoo の 320x200 フレーム) を解析すると、隣接ピクセル差は
  平均 0.3〜1.3 / p99 = 13 で、色は 1 次元のパレット曲線上
  (PC1 が分散の 97%) にあった。そこで 2 レイヤーの平均
  `(K + L) / 2` に変更。隣接差の平均は 0.7〜1.9 / p99 = 8〜12 に
  なり、オリジナルと同じ滑らかさになった。
- **まだ縦に長い**: オリジナルは 480 ラインの画面に 280 行だけ描いて
  上下に黒帯があり、走査線あたりの模様の進みがこちらの 1.4 倍あった。
  `PLZ_BAND_H = 150` (280/480) の帯にして `y` を 1.875 倍にした。
- **モノトーン (pals[2]) で白黒が反転する**: `build_base_palette(2)` の
  3 番目の区間で `ptau[63 - (a - 64)]` と書いており、a=128 以降で
  `ptau[]` の範囲外 (負のインデックス) を読んでいた。正しくは
  `ptau[191 - a]`。範囲外の値がパレットに混入して白/黒が壊れていた。
- **色切り替えが「下にスライドして消える」になっていない**:
  オリジナルは `COPPER.ASM` の `do_drop` + `dtau[]`
  (`dtau[c] = c*c/4*43/128 + 60`) でラインコンペアを 60 → 404 まで
  動かし、描画を下方向へスライドさせて画面外へ追い出す。
  同じ式 (`wipe_off = wipe*wipe*43/512`) で chunky の先頭を黒くし、
  残りを下へずらすワイプにした。ワイプ完了後に新しいパレットと
  パラメータへ切り替える。
- **色切り替えが 4 回に見える / モノトーンの位置**: 一度は
  `pals[0], pals[1], pals[3], pals[2]` の 4 段階にしていたが、
  オリジナルで表示されるのは RGB → RB-black → モノトーンの 3 段階。
  タイムテーブルを 723 / 1491 の 2 回にし、`pals[3]` (RB-white) を
  外した。WinUAE で 赤青系 → レインボー系 → モノトーン の順に
  2 回だけ切り替わることを確認。

### 12.12 U2A (Vector Part I - スペースバトル) — 実パート第 10 号 (2026-11-08)

- **オリジナル**: `VISU/C/U2A.C` (シーン再生) + `VISU/VISU.C`
  (`vis_drawobject`) + `ACALC.ASM` (回転・投影・行列) +
  `ADRAW.ASM` (ポリゴン描画・ライティング)。
- **資産変換** (`tools/u2a_prepare.py`):
  - `U2A.00M`: 768 バイトのパレット + オブジェクトリスト
    (`count=6, idx=[1,2,3,1,1]` → co[1..5] が s01/Sippi/moottori/s01/s01)。
  - `U2A.001..003`: `s01` (159 頂点/269 法線/272 三角形)、
    `Sippi` (285/64/158)、`moottori` (45/54/50)。
  - `U2ABG.UH`: 320x200 の背景。パレット 192..255 はこの画像から取る。
  - `U2A.0AB`: 521 フレーム / 1272 レコードのアニメーションストリーム。
  - 出力: `assets/u2a/u2a_{pal,bg,obj,anim,scene}.bin`。
  - `u2a_obj.bin` の形式: `u16 objcount`、各オブジェクト
    `u16 vnum, nnum, tnum, center`、`vnum*(s32 x,y,z)`、
    `nnum*(s16 x,y,z)`、`tnum*(u16 a,b,c,nrm, u8 color,flags)`。
    `center` は `ORD0` の最寄り頂点 (距離ソート用)。
- **投影**: オリジナルは fov=34 固定。`avistan[68] = 577`
  (`256/tan(23.9°)`) なので `projmulx = 160*577/256 = 360`、
  `projaddx = 159`、`projaddy = (25+174)/2 + 28 = 127`。
  z は 512 未満を近平面としてクリップする。
- **画面**: 背景は 28..227 行に置き、オブジェクトはオリジナルの
  viewport y=25..174 を +28 した 53..202 行にクリップする。
- **カメラ**: co[0] の `r0` がカメラ行列。アニメーションの
  オブジェクト 0 がこれを駆動する。各オブジェクトは
  `r = cam * r0`、`r.pos = cam.m * r0.pos >> 14 + cam.pos`。
- **描画**:
  - 頂点を UNIT=16384 の行列で回転 (`calc_rotate`)。
  - 法線も同じ行列で回転 (`calc_nrotate`)。
  - 透視投影 (`calc_project`)。
  - バックフェイスカリング: `dot(法線, 頂点) < 0` で可視。
  - フラットシェーディング: 光源 `(12118,10603,3030)`、
    `brightness = 128 + (dot >> 21)` を 0..255 にクランプ。
    `flags & 0x0c` でシェード段数 (0x04=>>5 / 0x08=>>4 / 0x0c=>>3)。
    色 = ポリゴンの基本色 + シェード (1..30)。
  - 近平面クリップ → 投影 → 2D クリップ (Sutherland-Hodgman) →
    スキャンライン塗り。`-nostdlib` なので 64bit 除算は使わない。
- **奥行きソート**: 中心頂点の z で降順 (遠い順) に挿入ソートして
  ペインターズアルゴリズムで描く。
- **アニメーション**: `0xff, fov` で 1 フレーム区切り、
  `0xff, 0xff` で終端。VBlank の経過分 (最大 4) だけ進める。
  `anim_advance()` は `lsget()` (可変長符号) で位置と行列の差分を
  累積する。
- **検証**:
  - 固定シーンで背景が `u2a_bg.bin` と一致 (上部は 100%)。
  - フレーム 300 で複数の船 (青・水色・グレー・ピンク) が
    表示されることを確認。
  - 通常ビルドは従来どおり ALKU から起動 (mean 5.71)。

---

### 12.13 イントロ後半の調整: 背景位置・PAM・プラズマ・BGM 切り替え (2026-11-08)

ユーザー指摘への対応。

- **ALKU 背景の位置**: オリジナル (`ALKU/ASMYT.ASM` の `outline`) は
  150 行の画像を 2 倍して 400 ライン画面の 50..349 行に置く。320x256 では
  同じ比率で 32..223 行に置き、上下を黒帯にした (`ALKU_BG_TOP 32`,
  `ALKU_BG_H 192`)。以前は 256 行へ引き伸ばしていたため背景だけが
  上に寄って見えた。
- **ALKU テキストの表示時間**: 1 ページ = フェードイン 64 + 保持 256 +
  フェードアウト 64 = 384 VBL (7.68 秒 = 音楽 1 オーダー) に延長。
- **ALKU 終了後**: 最後の景色を U2A まで保持 (フェードアウトしない)。
- **PAM の白フラッシュ**: オリジナル (`PAM/OUTTAA.C` の `wfade`) は
  `(63*a + (64-a)*pal)/64` で「白」と「画像」を混ぜる。以前は黒へ
  スケールしていたため、開始・終了が白にならなかった。
- **プラズマの横方向**: `PLZ.C` の `PLZSINI` は 1 ドットあたり
  A 側 +2 / B 側 -1。以前は A を +8 にしていたため模様が 4 倍細かく、
  `&4095` で正弦表の 1/4 しか使っていなかった。`&16383` (psini) /
  `&8191` (lsini) に修正。横方向の自己相関半値幅が 2..6px から
  19..33px (参考画像 33px) になり、オリジナルと同じ横長の模様になった。
- **BGM の切り替え**: オリジナル (`MAIN/U2.ASM` の `restartmus`) は
  パートごとに曲を切り替える:
  - 冒頭: MUSIC0 (skaven) を先頭から
  - BEGLOGO (タイトル) の後: MUSIC1 (purple motion) を order 0 から
  - フィナーレ: MUSIC0 を order 18 から
  `scene_cues[]` を `u32` にして bit24-25 を「音楽セクション」にし、
  上位バイト = オーダー、下位バイト = 行とした。U2A 11:47 / PAM 13:01 /
  タイトル 13:23 / トンネル (MUSIC1) 13:49 / 森 42:01 / プラズマ 62:00 /
  ドット 77:00 / 水 86:61 / 3D 93:00 と、ブラウザ移植 (`TIMELINE.js`) の
  キューに合わせた。タイトル表示直後 (skaven order 14 row 0) で
  `zinitmodule(music1) + zplaysong(0)`、pm 96:57 で MUSIC0 order 18 に戻す。
- **ビルドフラグ**: `-ffunction-sections -fdata-sections` (および
  `-fwhole-program`) を外した。これらを付けると `--gc-sections` が
  必要なコードを落とすらしく、メインループに曲切り替えを足した途端に
  起動直後 (TakeSystem 直後) で画面が真っ黒のまま止まった。外すと
  同じソースで正常に動作する。`-flto` は残している。
- **確認**: ALKU (order 0, speed 6) → U2A 92 秒 → PAM 106 秒 →
  タイトル 108 秒 → トンネル 152 秒。トンネル以降が MUSIC1 の
  テンポ (3 tick/row) で進むことを確認。

---

### 12.14 BGM テンポの再検証: MUSIC1 (PM) は原曲どおり (2026-11-09)

ユーザー報告:「スペースバトルの最後の爆発が終わってタイトルが出たあとの
次の BGM のテンポが速すぎる。2 倍くらい?」。原典と実測で確認した。

- **モジュールデータ**: MUSIC1 (Unreal 2 / PM) は tempo 130 / speed 3。
  order 0 のパターン 73 row 0 に `A03`。XOR デコード結果は ModArchive
  標準版と完全一致 (`tools/s3m_compare.py` 系で確認済み)。
- **オリジナルの tick レート**: STMIK は `zspeedadjust2` を 16bit
  アキュムレータに 70Hz リトレースごとに加算し、桁溢れで 1 tick。
  - MUSIC0: 45000 → 70*45000/65536 = 48.07 tick/s、speed 6 → 8.0 行/秒
  - MUSIC1: 48700 → 70*48700/65536 = 52.03 tick/s、speed 3 → 17.3 行/秒
  (ブラウザ移植の `IDEAL_TICK_FRAMES` 918/848 samples @44.1kHz と一致)
- **本移植の実測**: 50Hz VBL = 1 tick。トンネル区間 (t≈170s) で
  デバッグバーから np_speed=3 / 15.9 行/秒を実測 (`run/analyze-rb.py`、
  `run/sample-frames.ps1`)。オリジナルより 4〜8% **遅い**。
- **結論**: 「2 倍」は行レートの誤りではなく、MUSIC0 (8 行/秒) から
  MUSIC1 (17 行/秒) への切り替えそのもの (行レート比 2.0)。
  `MAIN/U2.ASM` でも `BEG.EXE` (タイトル画像のフェードイン、161
  リトレース = 2.3 秒) の直後に `restartmus(1, 0, 48700)` で MUSIC1
  order 0 を開始しており、オリジナルも同じ位置で同じ速い曲に変わる。
- **体感差の主因**: GLENZ 未移植のため、タイトル画像を約 45 秒間
  静止表示している (オリジナルは BEG 2.3 秒 + GLENZ の保持 ~6.5 秒で
  映像が切り替わり、速いパートは GLENZ の動く映像に乗る)。速い
  パート (order 4 以降) が静止画に乗るため「テンポが速い」と
  感じやすい。
- **今後の候補**:
  1. GLENZ を実装して映像と音楽を一致させる (本命、Very Hard)。
  2. MUSIC1 の開始をトンネルまで遅らせる (MUSIC0 は order 13 で
     実質終わるため、ループ処理が必要)。
  3. tick レートを STMIK 実測値 (48.07 / 52.03Hz) に合わせる
     (MUSIC1 は 4% 速く、MUSIC0 は 4% 遅くなる)。

---

### 12.15 MUSIC1 テンポの実音検証とロゴ保持時間の修正 (2026-11-09)

ユーザー報告:「MUSIC1 のテンポがまだ速い。MUSIC0 と同じ問題では？」

- **MUSIC0 の修正内容の再確認**: commit `a6e0558` は BPM を変えたのでは
  なく、鳴らすモジュールを差し替えた (`music1.srm` → 組み込み `MUSIC0`)。
  MUSIC1 は最初から正しいモジュール (原本 `REALITY.FC` の module 1、
  tempo 130 / speed 3) を使っており、BPM の変更点はない。
- **実音での検証** (`run/compare-tempo.py`, `run/record-loopback.py`):
  - 参照: OpenMPT (libopenmpt) で `2ND_PM.S3M` を再生した音源。
    低域オンセットの自己相関ピーク **232 ms** (= 130 BPM)。
  - 移植版: WinUAE の音声を WASAPI ループバックで録音。
    ピーク **238 ms**。約 2.6% 遅い。2 倍速なら 116 ms になるが、
    そうはならない。
  - 聴き比べ用: `run/tempo-ab.wav` (A=参照 8 秒 / B=移植版 8 秒 ×2)。
- **原因は BPM ではなく映像**:
  - オリジナルは BEG (2.3 秒) のあと MUSIC1 order 0 を開始し、
    PM 2:45 で GLENZ の映像に切り替わる (`GLENZ/MAIN.C` の
    `dis_musplus() >= -19`)。速いパートは動く映像に乗る。
  - 移植版は GLENZ 未移植のため、PM 13:49 までロゴ静止画のまま
    MUSIC1 を鳴らしていた (約 50 秒)。これが「速い曲だけが延々と
    続く」体感の原因。
- **修正**: TUNNELI のキューを PM 13:49 → **PM 2:45** に変更。
  ロゴはオリジナルと同じタイミングで終わり、以後はトンネルが動き続ける
  (曲はカットしない)。GLENZ を移植したらキューを 13:49 に戻し、
  GLENZ を 2:45 の位置に置く。

---

### 12.16 実音とオフライン再生の比較: プレイヤー自体は正しい (2026-11-09)

ユーザー報告「wav を聞いても移植版の方が明らかに速い」を受けて、プレイヤーを
ホスト (gcc) でオフライン再生し、実音と比較できるようにした。

- `tools/hostrender.c` + `tools/srm_to_le.py`: `player.c` をそのままホストで
  ビルドし、`audio.c` と同じ式で 15625Hz の WAV に書き出す
  (`gcc -O2 -o tools/hostrender.exe tools/hostrender.c src/m2/music/player.c`)。
- 比較 (`run/compare-tempo.py`、スペクトルフラックスのオンセット数):
  - 参照 (OpenMPT の `2ND_PM.S3M`): 8 秒で 55 オンセット (6.9/s)
  - ホスト再生: 59 オンセット (7.4/s) → **参照と一致**
  - WinUAE 実音: 106〜110 オンセット (13.2〜13.8/s) → **2 倍**
- つまりプレイヤーの行レート/ノート発音は正しく、差は Amiga の音声出力側。
  実音には約 **21Hz (48ms)** の周期的な変調があり、古い sine テスト WAV
  (`run/sine-*.wav`) にも同じピークが出ている。キャプチャ経路
  (WASAPI ループバック) 由来か、Paula DMA 由来かは切り分け中。
- 併せて `audio.c` のバッファ長を修正: PAL の VBL は 49.92Hz (313 ライン)
  なので 1 VBL = 313 サンプル、2 VBL = 626 サンプル (偶数) が Paula の
  AUDxLEN と正確に一致する。以前は 625 サンプルだけミックスし 626 個目を
  「直前サンプルの繰り返し」で埋めていたため、バッファ境界ごとに 1 サンプル
  の段差が入っていた。
- タイトルのキューは PM 2:45 (GLENZ 引き継ぎ) に変更 (§12.15)。

---

### 12.17 MUSIC1 のテンポ: tick レートと発音位置の量子化 (2026-11-09)

ユーザー報告「MUSIC0 は問題ないのに MUSIC1 だけ BPM が速い」の原因。

1. **tick レートが VBL 固定だった**: プレイヤーは 1 VBL = 1 tick (49.92Hz)
   で進み、`np_tempo` を時間計算に使っていなかった。S3M の tick 長は
   `2.5 / tempo` 秒なので、
   - MUSIC0: tempo 125 → 50.0Hz。49.92Hz とほぼ一致するので正常に聞こえる。
   - MUSIC1: tempo 130 → 52.0Hz。49.92Hz ではずれる。
2. **発音位置が 40ms 格子に量子化されていた**: ミキサーは 626 サンプル
   (= 40ms) をまとめて、バッファ先頭のプレイヤー状態でミックスしていた。
   - MUSIC0: 1 行 120ms (speed 6) = 40ms のちょうど 3 倍 → 誤差ゼロ。
   - MUSIC1: 1 行 60ms (speed 3) = 40ms の 1.5 倍 → 行ごとに 20ms ずれ、
     実効間隔が 40/80ms の交互になり「走って」聞こえる。

対策:
- `main.c`: tick を **経過 VBL 数**から計算するようにした。
  `1 VBL = tempo/124.8 tick` を Q16 で積算する。以前は
  「1 ループ = 1 tick」だったため、ループが VBL より速く回る環境や、
  シーンが軽い環境でテンポが速くなった。tick は必ず VBL 数に比例させる。
  (E-clock も試したが、`cpu_speed=max` では 1.57 倍のレートで進み
  実時間の基準にならなかったので採用しない。)
- `audio.c`: `mix_into()` を `mix_range()` に分割。DMA バッファの補充は
  VBL 数ではなくキューの空きで行う。AUDxLEN は開始時に一度だけ書き、
  VBL ごとの再書き込みをやめた (長さカウンタの再ロードでバッファ境界が
  崩れる)。
- `run/run-m0.ps1`: `m2.exe` が `m0.hdf` より新しければ HDF を自動再生成
  (古いビルドを実行してしまう事故の防止)。
- 実機検証: 画面のデバッグ表示から `np_speed=3` / `np_tempo=130` を確認。
  **ticks/VBL = 1.042**（期待 1.0417 = 130/124.8）で、シーンの負荷に
  よらず一定。旧実装は負荷でテンポが変動していた（重いシーンで遅く、
  軽いシーンで速い）。VBL = 48.9Hz、音声 = 15312 サンプル/秒
  (2 VBL に 1 バッファ)。
- 検証 (ホスト): 曲の長さ **404.83 秒**、参照 404.8 秒と 0.01% で一致。
- なお `A7F` は STMIK 仕様 (Axx は常にスピード、テンポは Txx) で
  スピード 127。S3M 規格の「A で 0x20 以上はテンポ」は適用しない
  (適用すると曲長が 367 秒になり参照と合わない)。

### 12.18 ツールのバグ: srm_to_le のオーダーリスト変換 (2026-11-09)

`tools/srm_to_le.py` が SRM のオーダーリスト (1 バイト/個) を u16 として
バイトスワップしていたため、ホスト再生 (`tools/hostrender`) は曲順が壊れた
モジュールを鳴らしていた。このため §12.14〜§12.16 のホスト比較は無効。
修正後は order 0 → 1 が 0.24 秒 (pattern 73 row 3 の C00) で正しく進み、
パターンブレイク・0xFE スキップも正常と確認できる。

### 12.19 音声クロックの測定 (2026-11-09)

`run/m0.uae` の `gfx_vsync=true` + `gfx_refreshrate=50` では、VBL は
49.82Hz (正しい PAL) だが、440Hz のテストトーンが 368.8Hz (0.833 倍) で
録音される。DMA がバッファ (40ms 分) を 47.8ms かけて再生するため、
バッファ切り替えごとに 16% が切り捨てられ、20.9Hz の周期的な音切れになる。
`gfx_vsync=false` では 438.3Hz (ほぼ正常) になる。設定は元に戻してある。

---

## 13. 実行環境の変更: KS 3.1 実 ROM + HDF 起動 (2026-11-07)

従来は WinUAE 内蔵 AROS ROM + 改造 AROS ブートフロッピーで起動していたが、
実機に近い環境にするため **Kickstart 3.1 実 ROM + HDF (ハードディスク)** に移行した。

- **ROM**: `AMIGA_KICKSTART` (例 `C:\path\to\kickstart31.rom`, 512KB) を
  `kickstart_rom_file=` で指定。`boot_rom_uae=min` は削除。
- **起動媒体**: `run/m0.hdf` (RDB + FFS パーティション `DH0`, bootable)。
  `run/make-hdf.ps1` が rdbtool / xdftool で作成する:
  1. `rdbtool create chs=130,16,63` → `init` → `add name=DH0 bootable=1`
  2. `change 0 dostype=0x444F5301` (DOS\1 = FFS)
  3. `xdftool format "Work" ffs` → `boot install`
  4. `S/Startup-Sequence` に `DH0:m2.exe`
  5. `m2.exe` + アセット (music1.srm / title.raw / endpic.raw / hoi.uh / fona.uh)
- **WinUAE 設定の要点**:
  - HDF は `hardfile2=rw,DH0:<path>,0,0,0,512,0,,` で接続する
    (旧 `hardfile=` / `uaehf0=` では認識されなかった)。
  - `kickstart_rom_file` は `quickstart=a1200,0` より後に書く。
- **データも HDF へ**: フロッピーの 816KB 制限がなくなり、`main.c` のパスは
  `DH0:music1.srm` などに変更。DF1:/DF2: は不要になった。
- **確認**: KS 3.1 の起動画面 → HDF から `m2.exe` が起動し、9 シーン巡回が動作。
- 旧方式 (`make-adf.ps1` / `make-data-adf.ps1`) はスクリプトとして残している。

---

*本文書は調査結果に基づく計画であり、実装の進行に応じて更新する。*

### 12.20 S3M の音量スライド (Dxy) を仕様どおりに修正: MUSIC0 のビープ音 (2026-11-09)

ユーザー報告「MUSIC0 で開始して数秒で変なビープ音が鳴る」「本来発音しない
ノートが鳴っている」。仕様書 (MultimediaWiki / Scream Tracker 3 Module,
ST3.21 の再生ルーチン) を一次資料にして修正した。

**仕様 (原文の要点)**
- `D0x` (1..E): 非 0 tick で x 下げる。`Dx0` (1..E): 非 0 tick で x 上げる。
- `DFx` (1..E): **tick 0 で x 下げる (ファイン)**。
- `DxF` (1..E): tick 0 で x 上げる (ファイン)。
- `DFF`: tick 0 で 15 上げる。`D0F`: 全 tick で 15 下げる。`DF0`: 全 tick で 15 上げる。
- `Dxy` (両ニブル 1..E): ST3 は **y 下げる** (D0y と同じ)。IT は何もしない。
- `D00`: 直前の非ゼロ Dxy パラメータを使う (エフェクトメモリ)。
- 高速スライド (cwtv == 0x1300 で自動有効): ファイン以外は tick 0 でも動く。
- `Qxy` は音量スライドではなく「y tick ごとにリトリガ + x で音量変更」。
  x は乗算/加算テーブル (0,-1,-2,-4,-8,-16,*2/3,*1/2,0,+1,+2,+4,+8,+16,*3/2,*2)。
- `Txx` は 33 未満を無視。`Cxy` は 10 進 (x*10+y)。

**原因**
- 旧実装は「x があれば毎 tick 上げる」だったため、MUSIC0 order 1 row 15 の
  `5:.. DF1` が **+15/tick** になり、ch5 の音量が 1 tick で最大まで跳ね上がって
  いた。ホスト再現 (`tools/hostrender`) で 9.46 秒 (= order 1 row 15) だけ
  RMS が 3.5 倍になることを確認。これがビープ音の正体。
- 併せて `D00` (MUSIC0 に 5385 個) が完全に無視されていた。本来は直前の
  非ゼロパラメータを継続するので、フェードイン/アウトが全部途中で止まっていた。
- `Qxy` を音量スライドとして解釈していた (Q03 → 毎 tick -3)。

**修正** (`src/m2/music/player.c`)
- `apply_vol_slide()` を仕様の表どおりに書き直し、`D00` 用のメモリ
  (`vol_slide_mem`) を追加。
- `Qxy` を専用実装 (`do_retrig()` + `apply_retrig_vol()`) に分離。
- ノート + 楽器で音量カラムが無いセルは楽器の既定音量に戻す
  (openmpt で実測: 音量カラム無し = vol 64 と同じ結果になることを確認)。
- `Txx` のしきい値を 32 → 33。

**MUSIC0 のテンポ**
- `MAIN/STARTMUS.C` が `module[50] = 0x78` で **MUSIC0 の初期テンポを 120 に
  書き換えている**。アセット (`music0.srm`) は 125 のままだったので 120 に修正。
- STMIK の `zspeedadjust2` (MUSIC0=45000 / MUSIC1=48700、70Hz の 16bit
  アキュムレータ) から tick レートは 48.07 / 52.03 Hz。S3M の
  `tick = 2.5/tempo` だと tempo 120 → 48.0Hz、130 → 52.0Hz で一致する。

**検証 (実機 WinUAE 録音)**
- 修正前: 0.25 秒 RMS が中央値の 2 倍を超えるフレームが 6 個
  (3.5-4.25 秒と 10.75-11.0 秒 = まさに DF1 の位置)。max/median = 2.41。
- 修正後: 2 倍超のフレーム 0 個、max/median = 1.66 (曲本来の強弱の範囲)。
- クリップ (|x| >= 0.99) は 0 サンプル。

**参考ツール**
- `tools/srm_to_s3m.py`: 復号済み `.srm` を標準 S3M に戻す。openmpt123 で
  同じデータを正解プレイヤーに鳴らして比較するため。
  (原本 `reference/.../MUSIC1.S3M` はパターン部が暗号化されているので
  openmpt に直接食わせると意味のない音になる。)

### 12.21 MUSIC1 のテンポ: tick を音声バッファ内でサンプル単位に置く (2026-11-09)

ユーザー報告「MUSIC1 のテンポが速い」。§12.17 で tick の *レート* は
正しくしたが、まだ速く聞こえていた。原因は tick の **発生位置** だった。

**原因**
- `audio.c` は 626 サンプル (= 2 VBL = 40.06ms) をまとめてミックスし、
  その時点のプレイヤー状態 1 つでバッファ全体を埋めていた。
  `mix_range(idx, start, count)` は「tick 境界で分割できる」設計だったが、
  実際には `mix_range(idx, 0, MIX_SAMPLES)` でしか呼ばれていなかった。
- つまりノートの発音位置は 40.06ms 格子に丸められる。
  - MUSIC0: 1 行 = 125.0ms (tempo 120 / speed 6) = バッファ 3.12 個。
    丸め誤差が徐々にずれるので目立たない。
  - MUSIC1: 1 行 = 57.69ms (tempo 130 / speed 3) = バッファ 1.44 個。
    行頭が次のバッファ境界に丸められ、**40.1ms と 80.1ms の交互**になる。
    平均は 60.1ms で合っているのに、40ms 側が 0.69 倍・80ms 側が 1.39 倍に
    なるため「走って聞こえる」。これが「1.33 倍くらい速い」の正体。

**修正** (`src/m2/music/audio.c`, `main.c`, `music.h`)
- `fill_buffer()` を追加。1 バッファを tick 境界で分割して `mix_range()` を
  呼び、tick が来たサンプル位置で `player_tick()` を進める。
  - `tick_acc` は「Q16 tick x 313 サンプル」単位。1 サンプルあたり
    `tempo*525*tempo_pct/100` ずつ増える。
  - 次の tick までのサンプル数 = `ceil((TICK_UNIT - tick_acc) / inc)`。
- `music_frame()` から `player_tick()` を外し、プレイヤーを音声バッファの
  生成 (= Paula DMA が消費する速度) に同期させた。
- `audio_set_tempo_pct()` を追加し、`DH0:tempo.cfg` の倍率を audio 側へ渡す。

**検証**
- オフラインシミュレーション (同じ式を Python で再現):
  - MUSIC1: 行間隔 57.7ms 一定 (修正前は 40/80ms 交互)
  - MUSIC0: 行間隔 125.0-125.1ms 一定
- 実機 (WinUAE + WASAPI ループバック) で MUSIC1 を頭から再生:
  peak 0.882 / クリップ 0 サンプル / max/median 1.33 / 2 倍超フレーム 0。
- ユーザー試聴で「テンポが正しい」と確認。

### 12.22 GLENZ (透明ポリゴン) の移植 (2026-11-09)

未移植だった GLENZ を追加した。オリジナルは `GLENZ/MAIN.C` + `VEC.ASM` +
`MATH.ASM` + `VIDPOLY.ASM`。

**オリジナルの仕組み**
- 14 点 (立方体 8 + 軸方向のトゲ 6) を回転・スケールし、24 枚の三角形に
  分けて投影する (`epolys`)。2 個目の大きいオブジェクト (`epolysb`) は
  frame > 800 から出る。
- 背景 `FC.UH` (64784 = 768 + 16 + 320x200) は **16 色しか使わない**。
  パレットはファイル offset 16 の 48 バイト (6bit)。
- 各ポリゴンは色ワードに **16 の倍数** (0x10..0xe0) を持ち、塗るときは
  chunky の値を XOR する。背景の 0..15 と XOR すると同じブロックの 0..15 に
  なるので、そのブロックのパレットを「面の明るさ + 背景色/4」にしておくと、
  重なった面が透けて明るく見える。これが Glenz。
- 面の明るさは投影後の**符号付き面積**から求める (`checkhiddenbx` の
  クロス積を 7bit シフト)。裏面も塗る (透明なので)。明るさの基準は
  `lightshift` 9/10 で 2 段階。
- 行列は `rY*rX*rZ` を 2^15 固定小数点で計算 (`calcmatrix`)、回転は
  `(m*x+m*y+m*z)>>15`。投影は `sx = x*256/z + 160`, `sy = y*213/z + 130`。
- タイムライン (回転 32/7 度/フレーム、ypos のバウンド、jello 変形、
  frame>800 で 2 個目、frame>900 で振動する移動、frame>2000 付近で縮小)。

**Amiga 版** (`src/m2/parts/glenz.c`)
- 320x256 の chunky に描いて c2p。背景は y=28 に中央配置。
- 三角/四角ポリゴンを走査線 + DDA で XOR 塗り。スパンは 32bit XOR。
- パレットは面ごとに 16 エントリのブロックを作り直す (明るさ 7 段 ×
  色味 2 種 = 14 ブロック)。ブロック番号は `1..7` (グレー) と
  `8..14` (青系, G を半分) で、XOR しても同じ範囲に収まる。
- 背景は最初の 1 回だけ全体をコピーし、以降は前フレームで塗った矩形
  (dirty rect) だけ戻す。
- 行列・投影・タイムラインはオリジナルの式をそのまま使う。

**検証**
- アセット: `tools/glenz_prepare.py` が `FC.UH` から
  `glenz_bg.bin` (64000) / `glenz_pal.bin` (768, 先頭 16 色) /
  `glenz_sin.bin` (3600 x s16 BE) を作る。
- `-DTEST_FIXED_SCENE=4` (GLENZ) で実機キャプチャ:
  - 背景 (R20 G12 B20) の上に、明るい面 (R85 G40 B85) が出る。
  - パレット上位色に R16 G0 B16 / R0 G0 B16 / R80 G80 B80 /
    R80 G32 B80 があり、XOR ブロックが機能している。
  - 連続キャプチャでオブジェクトが動く (差分 32k-51k px)。
  - フレームレート: マゼンタのバーで実測 **約 52fps**
    (10 秒で 520 フレーム)。
- シーン巡回に追加 (13 → 14 シーン)。キューはオリジナルどおり
  MUSIC1 order 2 row 45 (PM 2:45) で GLENZ、order 13 row 49 (PM 13:49) で
  TUNNELI。以前 TUNNELI に割り当てていた 2:45 を GLENZ に戻した。

**未対応**
- 冒頭の `zoomer2()` (FC ロゴのズームイン) は未移植。背景は最初から
  静止表示。`ZOOMLOOP.INC` (281KB の生成 asm) の再現が必要。
- `frame 765-795` のパレット切り替えワイプ (ライン単位) は簡略化。

### 12.23 JPLOGO (Ice Kingdom) の移植 (2026-11-11)

3DSINFLD の次、U2E の前に入る `JPLOGO` を追加した。

**オリジナル** (`JPLOGO/JP.C` + `ASM.ASM`)
- `ICEKNGDM.UP`: 320x400 / 256 色のパック画像 (ヘッダ 16 + パレット 768 +
  行ごとの RLE)。表示には各行 x=70..253 の 184 px を使う。
- 200 フレームの上下バウンス (y1/y2) を 4 倍補間して 800 サブフレームにし、
  各スキャンラインで `b=(y-y1)*400/(y2-y1)`,
  `a=184+(sin1024[b*32/25]*xsc+32)/64` (xsc=(400-(y2-y1))/8) の行ズーム。
- 70 Hz / 700 フレーム。PAL 50 Hz では `vbl*7/5` で同じ速度にした。

**Amiga 側** (`src/m2/parts/jplogo.c`)
- 400 ラインを 320x200 のレターボックス (y=28..227) に 2:1 で表示。
- 各行は 184 px を `a` px に最近傍拡大して x=160 中心に置く。
- アセットは `tools/jplogo_prepare.py` が `ICEKNGDM.UP` から
  `jplogo_pic.bin` (128000) / `jplogo_pal.bin` (768) /
  `jplogo_sin.bin` (2048, BE s16) を生成する。

**シーン / 曲**
- scene 14 に `jplogo` を挿入 (ddstars/end/scroll を +1)。
- MUSIC1 order 95 で JPLOGO に入り、500 VBL (10 秒) 後に
  `jplogo_finished()` で次へ。オリジナル `U2.ASM` と同じく
  MUSIC0 order 18 から再開する。
- U2E は未実装のため、暫定で DDSTARS (scene 15) に繋いでいる。

### 12.24 MUSIC1 のシーンキューを Modland 版に合わせる (2026-11-11)

`WATER (RAYSCRL)` が通常巡回で出ない原因を調査した。実装自体は
`src/m2/parts/water.c` にあり、オリジナルの `WATER/DEMO.PAS` +
`ROUTINES.ASM` (TAUSTA 背景 + WAT1-3 + MIEKKA の剣) を移植済み。

原因は **音楽データの版差**:
- オリジナル `MAIN/MUSIC1.S3M` は 100 オーダー中 15 個が `0xFE` (スキップ)。
- 使用中の `assets/music/MUSIC1_std.s3m` (Modland 標準版) は
  **パターン列は同一**だが `0xFE` が削除され、84 オーダーで終わる。
- そのため旧キュー (オリジナルの order 番号) は
  `WATER 86 / SINUS 93 / JPLOGO 95` が曲の終端 (order 83 = `0xFF`) を
  超えており、**一度も発火しなかった**。

**修正** (`src/m2/main.c`): MUSIC1 のキューを Modland のオーダー番号へ変換。
`TUNNELI 13->12, TECHNO 27->23, FOREST 42->35, LNS&ZOOM 47->39,
PLZPART 62->52, DOTS 77->65, WATER 86->74, SINUS 93->79, JPLOGO 95->81`。
row はパターンが同一なので変更なし。

### 12.25 U2E のビル消失: u2e_obj.bin の読み込み不足 (2026-11-13)

街中シーンの最高点 (frame 908、カメラ y=30005) でビルと地面が消える問題。

**原因**
- `src/m2/main.c` が `DH0:u2e_obj.bin` を **60000 バイト**でロードしていた。
  実ファイルは **67460 バイト** (`assets/u2e/u2e_obj.bin`)。
- `file_load_fast()` は要求サイズ分のバッファを `MEMF_CLEAR` で確保するため、
  後半 7460 バイト (オブジェクト 37..42) が**ゼロのまま**になった。
- ビル `talot04` (instance 40 → objs[39]) と `plushouse` (instance 39 →
  objs[38]) はこの範囲にあり、`vdata` が全部 0。
- `center_z()` が中心頂点 (0,0,0) を読み、距離がカメラ z の -6014 に。
  距離ソートと投影が崩れ、`vf_and` で画面外としてスキップされていた。

**修正**
- ロードサイズを 67460 に変更 (`main.c`)。
- 併せて原作の面処理を追加:
  - `ADRAW.ASM:475-476`: `color == 0xFFFF` の面は描かない。
  - `ADRAW.ASM:484-486` / C++ `VISU.cpp:1511`: `F_2SIDE`
    (生フラグ byte の bit1) はバックフェイスカリングしない。

**検証**
- frame 908 の `dist` が参照値 `talot04=17881` / `plushouse=10389` と一致。
- 画面に参照と同じ色 (198,206,218 / 89,85,97 / 68,97,125 /
  129,117,129 / 121,109,125) が復活。
- 診断には画面下部 y=224..255 のバーコードを使った (ビューポート外なので
  背景と混ざらない)。

### 12.26 GLENZ 冒頭の zoomer2 (2026-11-13)

GLENZ の冒頭に、原作 `GLENZ/ZOOMER.C:18-81` の zoomer2 を追加した。

**原作 / C++ 版**
- `BEG.C:25-34`: BEGLOGO はパレット 0..254 をタイトル画像の色にし、
  **255 を黒**にする (`setpalarea(palette,0,254)` は 255 を触らない)。
- `ZOOMER.C:35-58`: `zy` を加速しながら 0..260、`zy2=125*zy/260` を
  上下の帯として `memset(v,255,...)` で塗る。index 255 = **黒**。
- `ZOOMER.C:59-68`: パレット 0..127 を `(pal*b + 30*c)>>5`
  (c<=32) でグレー 30/63 へフェード。
- `MAIN.C:319-351` / C++ `GLENZ_MAIN.cpp:1237-1276`: 閉じた後は
  床板 (fc 画像) を `y1=130+y/2`, `y2=130+y*3/2` で表示し、
  `yy += ya` の加速で下へ落下させる。y=130 は 400 ライン画面の
  帯の隙間 (260..274) と同じ位置。
- C++ 版 `Parts/GLENZ/GLENZ_MAIN.cpp:1031-1095` も同じ式。

**Amiga 版** (`src/m2/parts/glenz.c`)
- 320x256 画面なので、400 ライン座標を `*256/400` で換算。
- タイトル画像は `title.raw` から chunky に変換して表示。
- 帯は index 255、パレット 255 は黒。パレット 0..127 は
  8bit 換算 `(pal*b + 121*c)/32` でグレーへ。
- 帯が閉じた後は黒でクリアし、既存の背景ズーム
  (`zoom_draw`) で床板を落下させる。`y2==y1` の最初のフレームでも
  床板先頭 16 行 (fcrow2) を描くようにした (`MAIN.C:344`)。

**検証**
- テスト用停止 (`GLENZ_STOP_WIPE2`) で帯が黒、タイトルがグレーに
  なることをピクセル解析で確認。
- 停止を外した通常ビルドで、閉じた後に床板が現れて落下することを
  実機 (WinUAE) で確認。

### 12.27 GLENZ の jello が縦に縮まない問題 (2026-11-13)

GLENZ の物体が床で跳ねるとき、本来は縦（画面 y）に縮むが、移植版では
縦に伸びていた。

**原因**
- 原作 GLENZ/VEC.ASM:123-160 の 
otlist は、行列 row1 (m3..m5) の
  結果を出力 **z** に、row2 (m6..m8) の結果を出力 **y** に入れる。
  projlist (VEC.ASM:200-221) は出力 y を画面縦に使う。
- 原作 GLENZ/MAIN.C:601-609 はスケール行列を
  matrix[4]=yscale, matrix[8]=zscale に置く。
  したがって画面縦に効くのは **zscale**:
  zscale = 120 - jello/30（jello>0 で縮む）。
- 移植版の 
ot_list は row1→y, row2→z と逆だったため、
  yscale = 120 + jello/30（伸びる値）が画面縦に、
  zscale（縮む値）が奥行きに掛かっていた。

**修正** (src/m2/parts/glenz.c)
- 
ot_list の出力を原作どおり row2→y, row1→z に入れ替え。

**検証**
- frame 407 (jello=908, zscale=90) を固定表示し、物体のバウンド時の
  バウンディングボックスを比較:
  - 修正前: 幅 164 / 高さ 125
  - 修正後: 幅 168 / 高さ 89
  縦だけが約 29% 縮み、横はほぼ不変。

### 12.28 音楽マーカー (np_zplus) / np_zframe と dis_musplus 待ち (2026-11-13/14)

**一部対応済み。**

- 原作 DIS の `dis_musplus()` は、**オーダーリストの 0xFE (スキップ)
  マーカー**までの残り行数を -32..+31 で返す。
- Modland 版 (`MUSIC0_std.s3m` / `MUSIC1_std.s3m`) は 0xFE が削除されて
  いるため、`player.c` に `zsetmarkers()` を追加し、削除された 0xFE の
  「直後のオーダー」を std 添字で登録する仕組みを入れた。
  - MUSIC0: 27
  - MUSIC1: 3,13,17,22,23,33,35,39,41,52,57,65,75,79,83
  - 登録は `main.c` のロード直後。
- **`np_zframe` は原作 STMIK と同じく tick ごとに進める**
  (`STMIK.300` の `0x4358: inc word ptr [0x08FB]`)。
  以前は `process_row()` で row ごとに進めていたため、speed 3 の
  MUSIC1 では 3 倍の時間になっていた。GLENZ の 300/333 待ちが
  19 秒 → 6.4 秒になり、最初のオブジェクトの出現が原作と同じになった。
- これで END (`END.C:110` の `>-16`) と GLENZ (`MAIN.C:353,400` の
  mframe 300/333) の待ちが動く。

### 12.29 TODO: 右クリックのスキップを「1 回 = 1 シーン」に固定する (2026-11-14)

**ユーザー依頼 (2026-11-14)。忘れないこと。**

- 現状: 右クリックで 1 シーン進めた直後、音楽キュー (g) が先に進んで
  いると汎用キュー切替 (g > cur_scene && g != scene_group) が即座に
  発火し、1 クリックで複数シーン飛ぶことがある。
- 目標: 右クリック 1 回で必ず 1 シーンだけ進む。
- 実装案:
  1. スキップ時に「音楽キューを新しいシーンのキューへ合わせる」
     (scene_cues の逆引きで order/row を求めて zgotosong する)。
  2. または、スキップ後の汎用キュー切替を「音楽が次のオーダーへ
     進むまで」抑止する (
p_ord の変化を待つ)。
  3. どちらも、パート固有の *_finished() 遷移は止めない。
- 注意: JPLOGO (14) は専用の MUSIC0 order 18 再開処理があるので、
  スキップ時も同じ処理を通すこと。
### 12.30 TECHNO の音楽同期 (2026-11-14)

**一部対応済み。**

- **TROLL の終了だけ原作マーカーに合わせた** (`KOE.C:706-709`)。
  `mus_pos() >= std 32:59` (original order 38) で PH_END にする。
  以前の 40 秒固定だと TROLL が長すぎ、MNTSCRL が音楽キュー
  (LNS&ZOOM) に追い越されて一瞬になっていた。安定版の遷移
  (PH_A/PH_B/DOIT) はそのままなので白画面は出ない。
- **未対応**: TECHNO の開始 (`KOE.C:213` `<-4`)、干渉円の終了
  (`KOE.C:275` `<-3`)、TROLL 開始 (`KOE.C:638` 絶対位置 35:48)
  の同期。これらを入れると PH_B→PH_TRANS→PH_DOIT の直後に
  **画面が白いまま止まる** (2026-11-14 に再現)。`start_wait` を
  外しても同じ。DOIT 単体 (固定シーン + 遷移) は動作するため、
  音楽位置と遷移の組み合わせが原因とみられる。要調査。
  - 参考: 開始を std 17:60 にすると、PH_A/PH_B が音楽 order 18-22
    の間に走り、遷移が order 22 付近になる。
  - MUSIC1 order 22 は 0xFE マーカー (original 25) の直後で、
    `np_zplus` が 1→2 に変わる位置でもある。

