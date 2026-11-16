import sys, wave
import numpy as np

path = sys.argv[1] if len(sys.argv) > 1 else 'run/title-audio.wav'
w = wave.open(path, 'rb')
sr = w.getframerate(); ch = w.getnchannels(); n = w.getnframes()
d = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float32).reshape(-1, ch).mean(axis=1)
dur = len(d) / sr
print('file', path, 'sr', sr, 'dur', round(dur, 2), 'rms', round(float(np.sqrt((d**2).mean())), 1))

# spectral-flux onset envelope
hop = 64
win = 1024
S = []
for i in range(0, len(d) - win, hop):
    x = d[i:i+win] * np.hanning(win)
    S.append(np.abs(np.fft.rfft(x))[:600])
S = np.array(S)
flux = np.maximum(0, np.diff(S, axis=0)).sum(axis=1)
flux = flux - flux.mean()

# autocorrelation
ac = np.correlate(flux, flux, 'full')[len(flux)-1:]
ac = ac / (ac[0] + 1e-9)
fps = sr / hop
print('envelope rate %.1f Hz' % fps)
print('autocorrelation peaks (period ms, ac):')
lo, hi = int(0.030 * fps), int(0.200 * fps)
peaks = []
for i in range(lo + 1, hi - 1):
    if ac[i] > ac[i-1] and ac[i] >= ac[i+1]:
        peaks.append((ac[i], i))
for a, i in sorted(peaks, reverse=True)[:8]:
    print('   %6.1f ms  ac %.3f' % (i / fps * 1000, a))

# modulation spectrum (FFT of envelope) 4..40 Hz
env = flux
E = np.abs(np.fft.rfft(env * np.hanning(len(env))))
f = np.fft.rfftfreq(len(env), 1 / fps)
sel = (f >= 4) & (f <= 40)
idx = np.argsort(E[sel])[::-1][:8]
ff = f[sel][idx]
ee = E[sel][idx]
print('modulation spectrum peaks (Hz, rel):')
mx = ee[0]
for a, b in sorted(zip(ff, ee), reverse=True):
    print('   %6.2f Hz  %.2f  (%.1f ms)' % (a, b / mx, 1000 / a))

# onset peak intervals
thr = flux.mean() + 1.0 * flux.std()
peaks_i = []
for i in range(1, len(flux) - 1):
    if flux[i] > thr and flux[i] >= flux[i-1] and flux[i] > flux[i+1]:
        if not peaks_i or (i - peaks_i[-1]) > int(0.030 * fps):
            peaks_i.append(i)
ioi = np.diff(peaks_i) / fps * 1000
if len(ioi):
    hist, edges = np.histogram(ioi, bins=np.arange(30, 200, 10))
    print('IOI histogram (ms):')
    for h, e in zip(hist, edges):
        if h: print('   %3d-%3d ms: %d' % (e, e + 10, h))
