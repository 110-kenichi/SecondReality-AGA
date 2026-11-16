"""Capture-path control test.

Play a 440 Hz sine from the PC to the default output device and record it with
the same WASAPI loopback device used for the WinUAE recordings.  If the
captured tone is 440 Hz the capture path is exact; if not, the capture path
itself resamples with a rate error.
"""
import sys
import threading
import time
import wave

import numpy as np
import pyaudiowpatch as pyaudio

DUR = 3.0
LOOPBACK_INDEX = 22


def main(out_path):
    p = pyaudio.PyAudio()
    info = p.get_device_info_by_index(LOOPBACK_INDEX)
    rate = int(info["defaultSampleRate"])
    out_info = p.get_default_output_device_info()
    osr = int(out_info["defaultSampleRate"])
    print("loopback:", info["name"], rate)
    print("output  :", out_info["name"], osr)

    chunks = []
    want = int(rate * (DUR + 1.5))

    def rec():
        s = p.open(
            format=pyaudio.paInt16,
            channels=2,
            rate=rate,
            input=True,
            input_device_index=LOOPBACK_INDEX,
            frames_per_buffer=1024,
        )
        got = 0
        while got < want:
            b = s.read(1024, exception_on_overflow=False)
            chunks.append(b)
            got += 1024
        s.stop_stream()
        s.close()

    th = threading.Thread(target=rec)
    th.start()
    time.sleep(0.4)

    t = np.arange(int(osr * DUR)) / osr
    mono = 0.25 * np.sin(2 * np.pi * 440.0 * t)
    sig = np.repeat(mono.astype(np.float32)[:, None], 2, axis=1)
    out = p.open(
        format=pyaudio.paFloat32,
        channels=2,
        rate=osr,
        output=True,
        output_device_index=out_info["index"],
        frames_per_buffer=1024,
    )
    out.write(np.ascontiguousarray(sig).tobytes())
    out.stop_stream()
    out.close()
    th.join()
    p.terminate()

    data = np.frombuffer(b"".join(chunks), dtype=np.int16).reshape(-1, 2)
    with wave.open(out_path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data.tobytes())
    print("wrote", out_path, data.shape[0] / rate, "s")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "run/ctl-tone.wav")
