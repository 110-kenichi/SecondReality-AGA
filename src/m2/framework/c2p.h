#ifndef C2P_H
#define C2P_H

#include "framework.h"

/*
 * chunky (width*height, 8bit/px) → 8 プレーン planar 変換
 * planes: プレーン p は planes + p * (width/8*height) に連続配置
 * width は 8 の倍数であること
 */
void c2p8(const u8 *src, u8 *planes, int width, int height);

/* 高速版 (4x4 ビット転置方式)。出力は c2p8 と完全に同一 */
void c2p8_fast(const u8 *src, u8 *planes, int width, int height);
void c2p8_rect(const u8 *src, u8 *planes, int width, int plane_h, int y0, int rows);
/* 320x256 専用: 32 ドット単位の列 bx0..bx1-1 だけを変換する */
void c2p8_cols(const u8 *src, u8 *planes, int y0, int rows, int bx0, int bx1);
/* 320x256 専用: 下位4プレーンだけ変換し、上位4プレーンは触れない */
void c2p4_fast(const u8 *src, u8 *planes);
void c2p4_cols(const u8 *src, u8 *planes, int y0, int rows, int bx0, int bx1);
/* 書いた範囲だけを変換するための記録 (320x256、裏画面 2 枚)。
 * パートは毎フレーム、chunky に書いた矩形を c2p_track_add で登録し、
 * 最後に c2p_track_flush を呼ぶ。裏画面ごとに「前回その裏画面を
 * 変換してから書いた矩形」だけを変換する。書いていない場所は chunky も
 * 裏画面も変わっていないので、全体を変換したのと同じプレーンになる。
 * 初めて使う裏画面は 256 行全体を変換する。 */
typedef struct {
    u8 *buf[2];
    s16 r[2][3][4];             /* y0, y1, x0, x1 (y1, x1 は含まない) */
    u8 n[2];
} c2p_track;
void c2p_track_reset(c2p_track *t);
void c2p_track_add(c2p_track *t, int y0, int y1, int x0, int x1);
void c2p_track_flush(c2p_track *t, const u8 *chunky, u8 *planes);
/* 同じだが、変換した行の範囲 (y0..y1-1、無ければ y1 <= y0) を返す
 * (ファスト RAM の写しからチップ RAM へ写す範囲を決めるため)。 */
void c2p_track_flush_rows(c2p_track *t, const u8 *chunky, u8 *planes,
                          int *ry0, int *ry1);
/* 同じ追跡処理だが、c2p と同様に下位4プレーンだけを書き込む。 */
void c2p_track_flush_rows4(c2p_track *t, const u8 *chunky, u8 *planes,
                           int *ry0, int *ry1);

/* C 版 (アセンブラ版との比較・測定用) */
void c2p8_rect_c(const u8 *src, u8 *planes, int width, int plane_h, int y0, int rows);

/* 自己テスト: c2p8 と c2p8_fast の出力一致確認 + 所要フレーム数計測。
 * 戻り値 0 = 一致、それ以外 = 最初の不一致位置 + 1。
 * old_frames / new_frames に 3 回ずつの所要 VBlank フレーム数を返す。 */
int c2p_selftest(u32 *old_frames, u32 *new_frames);

#endif
