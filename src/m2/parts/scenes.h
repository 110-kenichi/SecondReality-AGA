#ifndef SCENES_H
#define SCENES_H

#include "../framework/framework.h"

/* シーン API (M3)
 *
 * render は chunky に描いて 0 を返す (呼び出し側が c2p する) か、
 * planes (8 プレーン) に直接描いて 1 を返す。
 */
typedef struct {
    const char *name;
    void (*init)(void);
    int (*render)(u8 *chunky, u8 *planes, int width, int height);
} Scene;

extern const Scene scene_list[];
extern const int scene_count;

/* TECHNO の終了 (TROLL の TV-off 後) を main が検出するためのフック。 */
int forest_finished(void);
int techno_finished(void);
void techno_prepare(void);
/* PANIC の終了 (TV-off 後) を main が検出するためのフック。 */
int panic_finished(void);
int jplogo_finished(void);
int plzcub_finished(void);
int dots_finished(void);
int ddstars_finished(void);
int u2e_finished(void);
int pam_finished(void);
int u2a_finished(void);
int alku_finished(void);
int image_finished(void);
int glenz_finished(void);
int lens_finished(void);
int water_finished(void);
int sinus_finished(void);
void u2a_apply_palette(void);
int tunnel_finished(void);
int credits_finished(void);
int end_finished(void);

void prof_mark(const char *tag);   /* main.c: 実機プロファイラの区切り記録 */

void alku_release(void);
void forest_release(void);
void water_release(void);
void techno_release(void);
void credits_release(void);

#endif
