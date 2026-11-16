#ifndef MEM_H
#define MEM_H
#include "framework.h"

/* AllocMem の代わり。確保したものを表に覚えておき、終了時に
 * mem_free_all() でまとめて返す。以前は返していなかったため、
 * 実機で 2 回目に起動するとチップ/ファスト RAM が足りずに
 * クラッシュした (2026-11-15 ユーザー報告)。 */
void *mem_alloc(u32 size, u32 flags);
void mem_free(void *p, u32 size);
void mem_free_all(void);

#endif
