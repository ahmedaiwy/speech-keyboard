import sounddevice as sd
import numpy as np

fs = 44100  # Sample rate
seconds = 3  # Duration of recording

print("Recording...")
myrecording = sd.rec(int(seconds * fs), samplerate=fs, channels=2)
sd.wait()  # Wait until recording is finished
print("Recording complete.")

print("Playing back...")
sd.play(myrecording, fs)
sd.wait()  # Wait until playback is finished
print("Playback complete.")
