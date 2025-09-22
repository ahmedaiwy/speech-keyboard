import soundfile as sf

# Read an audio file
data, samplerate = sf.read('sample-3s.wav')
print(data)
