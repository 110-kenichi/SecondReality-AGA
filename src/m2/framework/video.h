#ifndef VIDEO_H
#define VIDEO_H

#include "framework.h"

/* 画面初期化 (チップメモリ確保 + コッパーリスト構築)。成功で 1、失敗で 0 */
int video_init(void);
void video_shutdown(void);

/* 描画用チャンキーバッファ (Fast RAM, SCREEN_W*SCREEN_H バイト) */
u8 *video_chunky(void);

/* c2p の書き込み先 (裏画面の 8 プレーン先頭) */
u8 *video_back_planes(void);

/* 表画面の 8 プレーン先頭 */
u8 *video_front_planes(void);

/* 垂直帰線待ち → 裏画面を表示 → 表裏交換 */
void video_flip(void);

/* 両画面の 8 プレーンを 0 クリア (シーン切り替え用) */
void video_clear_planes(void);
u8 *video_back_shadow(void);
void video_commit_back_shadow(void);
void video_set_commit_area(int nplanes, int y0, int rows);

/* パレット設定 (rgb: 256*3 バイト, 各成分 8bit, AGA 24bit パレット) */
void video_set_palette(const u8 *rgb);

/* 現在のパレット (256*3, 8bit) を返す。部分パートが直前の色を
 * 引き継ぐとき (PANICEND) に使う。 */
const u8 *video_get_palette(void);

/* デバッグ用: 背景色 (COLOR00) を設定 */
void video_set_bg(u16 color);

/* 現在のパレットにフェード係数 (0-64) を掛けて反映 (シーン遷移用) */
void video_apply_fade(u8 level);

/* カスタムビットプレーン表示 (ハードウェア横スクロール用)
 * planes: 8 プレーン連続のチップメモリ / stride_bytes: 1 行のバイト数
 * planes=0 で通常のダブルバッファ表示に戻す */
void video_set_custom_screen(u8 *planes, int stride_bytes);

/* プレーン高さを指定する版 (縦スクロール用) */
void video_set_custom_screen_rows(u8 *planes, int stride_bytes, int rows);
void video_set_custom_hires(u8 *planes, int stride_bytes, int rows); /* 640x256 hires 4 planes */

/* 表示バッファとスクロール位置を同時に切り替える (ダブルバッファ用) */
void video_switch_custom_screen(u8 *planes, int stride_bytes, int scroll_x);

/* カスタム画面 (ハードウェアスクロール) 表示中か */
int video_custom_screen_active(void);

/* カスタム画面の横スクロール位置 (ピクセル単位、次のフレームから反映) */
void video_set_scroll(int x);
/* 画面分割: 表示 130 行目から下を別の画面 (8 プレーン、1 行 40 バイト、
 * プレーン plane_rows 行) の first_row 行目から出す。128, 129 行目は黒。
 * planes = 0 で解除。video_set_custom_screen(0, 0) でも解除される。 */
void video_set_split(u8 *planes, int plane_rows, int first_row);

/* カスタム画面の縦スクロール位置 (行単位、次のフレームから反映) */
void video_set_scroll_y(int y);

/* 標準画面の表示開始走査線を y 行下げる (ハードウェア縦シフト)。
 * 0 で通常。PLZPART の下方向スライドアウト用。 */
void video_set_display_start(int y);

#endif
