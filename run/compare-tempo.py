"""Compare the Amiga port's recorded audio against an OpenMPT render of the
same S3M module.  Reports, for each window, the onset-envelope autocorrelation
peaks and a comb-filter tempo estimate, plus the best alignment against the
reference.

usage: python run/compare-tempo.py <port.wav> <ref.mp3>
"""
import sys
import numpy as np
import soundfile as sf

SR = 44100
HOP = 256
WIN = 2048


def load_mono(path, max_sec=None):
    y, sr = sf.read(path, always_2d=True)
    y = y.mean(axis=1).astype(np.float32)
    if max_sec:
        y = y[: int(sr * max_sec)]
    if sr != SR:
        n = int(len(y) * SR / sr)
        y = np.interp(np.linspace(0, 1, n), np.linspace(0, 1, len(y)), y).astype(np.float32)
    return y


def onset_env(y):
    w = np.hanning(WIN)
    rows = []
    for i in range(0, len(y) - WIN, HOP):
        X = np.abs(np.fft.rfft(y[i:i + WIN] * w))[:800]
        rows.append(X)
    E = np.array(rows)
    f = np.maximum(0, np.diff(E, axis=0)).sum(axis=1)
    return f - f.mean()


FPS = SR / HOP


def peaks(env, lo_ms=150, hi_ms=900, n=6):
    ac = np.correlate(env, env, 'full')[len(env) - 1:]
    ac /= ac[0] + 1e-9
    lo, hi = int(lo_ms / 1000 * FPS), int(hi_ms / 1000 * FPS)
    out = []
    for i in range(lo + 1, hi - 1):
        if ac[i] > ac[i - 1] and ac[i] >= ac[i + 1]:
            out.append((ac[i], i / FPS * 1000))
    out.sort(reverse=True)
    return out[:n], ac


def tempo_comb(env, lo_bpm=60, hi_bpm=200):
    """Score each candidate beat period by autocorrelation at 1x/2x/4x."""
    ac = np.correlate(env, env, 'full')[len(env) - 1:]
    ac /= ac[0] + 1e-9
    best = []
    for bpm in np.arange(lo_bpm, hi_bpm + 0.01, 0.25):
        p = 60.0 / bpm * FPS
        s = 0.0
        for mult in (1, 2, 4):
            k = int(round(p * mult))
            if 0 < k < len(ac):
                s += ac[k]
        best.append((s, bpm))
    best.sort(reverse=True)
    return best[:5]


def main():
    port_path, ref_path = sys.argv[1], sys.argv[2]
    port = load_mono(port_path)
    ref = load_mono(ref_path, 45)
    print('port %.1fs  ref %.1fs' % (len(port) / SR, len(ref) / SR))

    ref_env = onset_env(ref)
    rp, _ = peaks(ref_env)
    print('REF peaks:', ' '.join('%.0fms(%.2f)' % (ms, a) for a, ms in rp))
    print('REF tempo:', ' '.join('%.2f(%.3f)' % (bpm, s) for s, bpm in tempo_comb(ref_env)))

    win = int(12 * FPS)
    step = int(6 * FPS)
    for start in range(0, max(1, len(port) // HOP - win), step):
        seg = port[start * HOP:(start + win) * HOP]
        env = onset_env(seg)
        if len(env) < 50:
            continue
        pk, _ = peaks(env)
        tc = tempo_comb(env)
        # alignment vs reference (first 40 s)
        best = None
        for lag in range(0, len(ref_env) - len(env), 2):
            a = ref_env[lag:lag + len(env)]
            c = float(np.dot(env, a) / (np.linalg.norm(env) * np.linalg.norm(a) + 1e-9))
            if best is None or c > best[0]:
                best = (c, lag)
        print('t=%5.1f-%5.1fs  tempo %6.1f  peaks %s  ref@%.1fs corr %.2f'
              % (start * HOP / SR, start * HOP / SR + 12,
                 tc[0][1], ' '.join('%.0f' % ms for a, ms in pk),
                 best[1] / FPS, best[0]))


if __name__ == '__main__':
    main()
