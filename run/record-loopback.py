import sys, wave, time
import pyaudiowpatch as pyaudio

out = sys.argv[1] if len(sys.argv) > 1 else 'run/loop.wav'
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
p = pyaudio.PyAudio()
dev = p.get_default_wasapi_loopback()
print('loopback device:', dev['index'], dev['name'], dev['defaultSampleRate'], dev['maxInputChannels'])
sr = int(dev['defaultSampleRate'])
ch = min(2, dev['maxInputChannels'])
stream = p.open(format=pyaudio.paInt16, channels=ch, rate=sr,
                frames_per_buffer=1024, input=True,
                input_device_index=dev['index'])
w = wave.open(out, 'wb')
w.setnchannels(ch); w.setsampwidth(2); w.setframerate(sr)
frames = int(sr * secs)
got = 0
t0 = time.time()
while got < frames:
    data = stream.read(min(1024, frames - got), exception_on_overflow=False)
    w.writeframes(data)
    got += 1024
w.close(); stream.stop_stream(); stream.close(); p.terminate()
print('wrote', out, 'sr', sr, 'ch', ch, 'secs', round(time.time() - t0, 1))
