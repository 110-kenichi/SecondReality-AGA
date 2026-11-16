#include "scenes.h"

void stars_init(void);
int stars_render(u8 *chunky, u8 *planes, int width, int height);
void plasma_init(void);
int plasma_render(u8 *chunky, u8 *planes, int width, int height);
void image_init(void);
int image_render(u8 *chunky, u8 *planes, int width, int height);
void end_init(void);
int end_render(u8 *chunky, u8 *planes, int width, int height);
void alku_init(void);
int alku_render(u8 *chunky, u8 *planes, int width, int height);
void u2a_init(void);
int u2a_render(u8 *chunky, u8 *planes, int width, int height);
void sinus_init(void);
int sinus_render(u8 *chunky, u8 *planes, int width, int height);
void glenz_init(void);
int glenz_render(u8 *chunky, u8 *planes, int width, int height);
void tunnel_init(void);
int tunnel_render(u8 *chunky, u8 *planes, int width, int height);
void scroll_init(void);
int scroll_render(u8 *chunky, u8 *planes, int width, int height);
void dots_init(void);
int dots_render(u8 *chunky, u8 *planes, int width, int height);
void water_init(void);
int water_render(u8 *chunky, u8 *planes, int width, int height);
void forest_init(void);
int forest_render(u8 *chunky, u8 *planes, int width, int height);
void lens_init(void);
int lens_render(u8 *chunky, u8 *planes, int width, int height);
void ddstars_init(void);
int ddstars_render(u8 *chunky, u8 *planes, int width, int height);
void pam_init(void);
int pam_render(u8 *chunky, u8 *planes, int width, int height);
void techno_init(void);
int techno_render(u8 *chunky, u8 *planes, int width, int height);
void panic_init(void);
int panic_render(u8 *chunky, u8 *planes, int width, int height);
void jplogo_init(void);
int jplogo_render(u8 *chunky, u8 *planes, int width, int height);
void u2e_init(void);
int u2e_render(u8 *chunky, u8 *planes, int width, int height);
void credits_init(void);
int credits_render(u8 *chunky, u8 *planes, int width, int height);

const Scene scene_list[] = {
    /* オリジナル (U2.ASM のロード順) に合わせる:
     *   ALKU -> U2A -> PAM -> BEGLOGO -> GLENZ -> TUNNELI -> TECHNO ->
     *   PANIC -> MNTSCRL -> LNS&ZOOM -> PLZPART -> MINVBALL -> RAYSCRL ->
     *   3DSINFLD -> JPLOGO -> U2E -> ENDPIC -> ENDSCRL
     * 未実装の JPLOGO / U2E は飛ばしている。 */
    { "alku",   alku_init,   alku_render   },   /* 0 ALKU */
    { "u2a",    u2a_init,    u2a_render    },   /* 1 U2A */
    { "pam",    pam_init,    pam_render    },   /* 2 PAM */
    { "image",  image_init,  image_render  },   /* 3 BEGLOGO */
    { "glenz",  glenz_init,  glenz_render  },   /* 4 GLENZ */
    { "tunnel", tunnel_init, tunnel_render },   /* 5 TUNNELI */
    { "techno", techno_init, techno_render },   /* 6 TECHNO */
    { "panic",  panic_init,  panic_render  },   /* 7 PANIC (Panicfake) */
    { "forest", forest_init, forest_render },   /* 8 MNTSCRL */
    { "lens",   lens_init,   lens_render   },   /* 9 LNS&ZOOM */
    { "plasma", plasma_init, plasma_render },   /* 10 PLZPART */
    { "dots",   dots_init,   dots_render   },   /* 11 MINVBALL */
    { "water",  water_init,  water_render  },   /* 12 RAYSCRL */
    { "sinus",  sinus_init,  sinus_render  },   /* 13 3DSINFLD */
    { "jplogo", jplogo_init, jplogo_render },   /* 14 JPLOGO */
    { "ddstars", ddstars_init, ddstars_render },/* 15 DDSTARS (hidden) */
    { "end",    end_init,    end_render    },   /* 16 ENDLOGO */
    { "scroll", scroll_init, scroll_render },   /* 17 ENDSCRL */
    { "u2e",    u2e_init,    u2e_render    },   /* 18 U2E (Vector Part II) */
    { "credits", credits_init, credits_render },  /* 19 CRED */
};

const int scene_count = 20;
