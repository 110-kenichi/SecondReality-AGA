#ifndef FRAMEWORK_H
#define FRAMEWORK_H

/* 基本型 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef short s16;
typedef int s32;

/* 画面設定 (M1: AGA 320x256 / 8 プレーン / 256 色 / PAL) */
#define SCREEN_W 320
#define SCREEN_H 256
#define SCREEN_PLANES 8
#define PLANE_BYTES (SCREEN_W / 8 * SCREEN_H)

#endif
