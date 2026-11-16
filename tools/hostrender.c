/* Host-side renderer for the Amiga S3M player.
 * Renders a .srm module to a 16-bit mono WAV using the same player_tick()
 * and the same per-sample mixing math as src/m2/music/audio.c.
 *
 * build (msys2/mingw gcc):
 *   gcc -O2 -o tools/hostrender.exe tools/hostrender.c src/m2/music/player.c -lm
 * usage:
 *   tools/hostrender.exe <module.srm> <out.wav> [seconds] [tick_hz]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/m2/music/music.h"

extern MixVoice mix_voices[8];

static void wav_write(const char *path, const signed char *pcm, int n, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    int databytes = n * 2;
    unsigned char h[44];
    memcpy(h + 0, "RIFF", 4);
    unsigned int riff = 36 + databytes;
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    unsigned int sz = 16; memcpy(h + 16, &sz, 4);
    unsigned short fmt = 1; memcpy(h + 20, &fmt, 2);
    unsigned short ch = 1; memcpy(h + 22, &ch, 2);
    unsigned int sr = rate; memcpy(h + 24, &sr, 4);
    unsigned int br = rate * 2; memcpy(h + 28, &br, 4);
    unsigned short ba = 2; memcpy(h + 32, &ba, 2);
    unsigned short bps = 16; memcpy(h + 34, &bps, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &databytes, 4);
    fwrite(h, 1, 44, f);
    for (int i = 0; i < n; i++) {
        short v = (short)pcm[i] * 256;
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "assets/music/converted/music1.srm";
    const char *out = argc > 2 ? argv[2] : "run/host.wav";
    double secs = argc > 3 ? atof(argv[3]) : 8.0;
    double tickhz = argc > 4 ? atof(argv[4]) : 52.03;
    double tscale = argc > 5 ? atof(argv[5]) : 1.0;   /* テンポ倍率 (ピッチは変えない) */

    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *mod = malloc(n);
    if (fread(mod, 1, n, f) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
    fclose(f);

    if (!zinitmodule(mod)) { fprintf(stderr, "zinitmodule failed\n"); return 1; }
    zsoundon();
    zplaysong(0);

    const int rate = 15625;
    const int per_vbl = 312;                 /* 20 ms */
    int total_vbl = (int)(secs * 50.0 + 0.5);
    int total_samples = total_vbl * per_vbl;
    signed char *pcm = malloc(total_samples);
    int sp = 0;
    unsigned tick_pos = 0;
    int ticks = 0;
    player_tick();                           /* バッファ先頭 = 行の頭 */
    ticks++;

    for (int v = 0; v < total_vbl; v++) {
        for (int i = 0; i < per_vbl; i++) {
            /* S3M の tick 長 = 2.5/tempo 秒。15625Hz では 39062.5/tempo
             * サンプル。audio.c の mix_into と同じ式。 */
            unsigned tempo = np_tempo ? (unsigned)np_tempo : 125u;
            unsigned tstep = (unsigned)(39062500.0 * tscale / (double)tempo);
            if (tick_pos >= tstep) {
                tick_pos -= tstep;
                u16 prev_ord = np_ord;
                player_tick();
                ticks++;
                if (np_ord != prev_ord)
                    fprintf(stderr, "t=%.2f ord=%d row=%d\n",
                            (double)(v * per_vbl + i) / 15625.0,
                            (int)np_ord, (int)np_row);
            }
            tick_pos += 1000u;
            int sum = 0;
            for (int c = 0; c < 8; c++) {
                MixVoice *mv = &mix_voices[c];
                if (!mv->active || !mv->data)
                    continue;
                unsigned int p = mv->pos >> 16;
                if (p >= mv->length) { mv->active = 0; mv->ended = 1; continue; }
                sum += ((int)(signed char)mv->data[p]) * mv->gain;
                mv->pos += mv->step;
                unsigned int end = mv->loop ? mv->loop_end : mv->length;
                if ((mv->pos >> 16) >= end) {
                    if (mv->loop)
                        mv->pos = mv->loop_start << 16;
                    else {
                        mv->active = 0;
                        mv->pos = 0;
                        mv->ended = 1;
                    }
                }
            }
            int s = sum >> 8;
            if (s > 127) s = 127;
            if (s < -128) s = -128;
            pcm[sp++] = (signed char)s;
        }
    }
    wav_write(out, pcm, sp, rate);
    fprintf(stderr, "wrote %s: %d samples %.2fs ticks %d (%.2f Hz)\n",
            out, sp, (double)sp / rate, ticks, (double)ticks / secs);
    return 0;
}
