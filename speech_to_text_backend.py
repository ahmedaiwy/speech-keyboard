import sys
import os
import numpy as np
import speech_recognition as sr
import requests
import json
import threading
import zipfile
import pyaudio
import time
import wave
from pydub import AudioSegment
from pydub.exceptions import CouldntDecodeError
import tempfile
import io
import subprocess # Kept for system requirements check
import base64 # <--- ADDED THIS LINE

# --- UPDATED PYDUB PATH SETTING ---
# Instead of importing set_ffmpeg_path, we set the converter directly on AudioSegment
AudioSegment.converter = "/usr/local/bin/ffmpeg"
# -----------------------------------

from flask import Flask, render_template, request, jsonify
from werkzeug.exceptions import HTTPException

# PyQt5 imports (only if running the GUI part)
PYQT_AVAILABLE = False
try:
    from PyQt5 import QtWidgets, QtCore
    from PyQt5.QtWidgets import QMessageBox
    from PyQt5.QtGui import QPixmap
    import pyperclip
    PYQT_AVAILABLE = True
except ImportError:
    print("PyQt5 or pyperclip not found. Running in web-only mode.")

from vosk import Model, KaldiRecognizer, SetLogLevel

SetLogLevel(0)

# --- Flask Setup ---
app = Flask(__name__)

# Directory to store downloaded Vosk models
MODEL_BASE_DIR = os.path.join(os.getcwd(), "vosk_models")

# Global Vosk model instance for Flask app (for efficiency)
flask_vosk_models = {}

# Global state for the web version (to mimic some desktop app behaviors)
web_is_saving_to_file = False
web_output_file = None
web_last_recognized_phrase = ""
web_online_mode = False # This will be controlled by the C++ app

# Queue for audio chunks to be processed by the worker thread
audio_chunk_queue = []
audio_chunk_queue_lock = threading.Lock()
audio_chunk_queue_cv = threading.Condition(audio_chunk_queue_lock)

# Global variable to store the latest transcription result
latest_transcription_result = {"status": "idle", "text": ""}
latest_transcription_lock = threading.Lock()

# --- Vosk Model Loading (for local fallback/testing) ---
def load_vosk_model(model_path):
    """Loads a Vosk model."""
    if not os.path.exists(model_path):
        print(f"Vosk model not found at {model_path}. Attempting to download...")
        # Simplified download: In a real app, you'd download a specific model.
        # For this example, we'll just indicate it's missing.
        print("Please download a Vosk model (e.g., vosk-model-en-us-0.22) and extract it to the 'vosk_models' directory.")
        return None
    try:
        model = Model(model_path)
        print(f"Vosk model loaded from {model_path}")
        return model
    except Exception as e:
        print(f"Error loading Vosk model from {model_path}: {e}")
        return None

# Load a default Vosk model for the backend if available
# This is mainly for the backend's own internal use if it were to do Vosk directly
# For Google Speech Recognition, this is not strictly needed.
# flask_vosk_models['default'] = load_vosk_model(os.path.join(MODEL_BASE_DIR, "vosk-model-en-us-0.22"))


# --- Flask Routes ---
@app.route('/')
def index():
    return "Speech-to-Text Backend is running."

@app.route('/audio', methods=['POST'])
def receive_audio():
    global web_online_mode
    if not web_online_mode:
        return jsonify({"status": "error", "message": "Online mode is not active"}), 400

    data = request.json
    if not data or 'audio_chunk' not in data:
        return jsonify({"status": "error", "message": "No audio_chunk provided"}), 400

    audio_data_base64 = data['audio_chunk']
    try:
        # Decode the Base64 audio data
        audio_data = base64.b64decode(audio_data_base64)
        
        # Add to queue for processing
        with audio_chunk_queue_lock:
            audio_chunk_queue.append(audio_data)
            audio_chunk_queue_cv.notify() # Signal the worker thread

        return jsonify({"status": "success", "message": "Audio chunk received"}), 200
    except Exception as e:
        print(f"Error decoding audio data: {e}")
        return jsonify({"status": "error", "message": f"Error decoding audio data: {e}"}), 400

@app.route('/transcription', methods=['GET'])
def get_transcription():
    global latest_transcription_result
    with latest_transcription_lock:
        response = latest_transcription_result
        # Reset after sending, so the client only gets new results
        latest_transcription_result = {"status": "idle", "text": ""}
        return jsonify(response), 200

@app.route('/mode', methods=['POST'])
def set_mode():
    global web_online_mode
    data = request.json
    if not data or 'online_mode' not in data:
        return jsonify({"status": "error", "message": "No online_mode provided"}), 400

    web_online_mode = bool(data['online_mode'])
    print(f"Backend: Online mode set to {web_online_mode}")

    # If switching off online mode, clear the queue and reset transcription
    if not web_online_mode:
        with audio_chunk_queue_lock:
            audio_chunk_queue.clear()
        with latest_transcription_lock:
            latest_transcription_result = {"status": "idle", "text": ""}

    return jsonify({"status": "success", "online_mode": web_online_mode}), 200

# --- Speech Recognition Worker Thread ---
def speech_recognition_worker():
    global latest_transcription_result
    recognizer = sr.Recognizer()

    while True:
        try:
            current_audio_chunk = None
            with audio_chunk_queue_lock:
                # Wait for a new audio chunk if the queue is empty
                while not audio_chunk_queue and web_online_mode:
                    audio_chunk_queue_cv.wait()
                
                if audio_chunk_queue:
                    current_audio_chunk = audio_chunk_queue.pop(0)
                
            if current_audio_chunk and web_online_mode:
                # Convert raw audio bytes to AudioData format for speech_recognition
                # Assuming 16kHz, 16-bit mono audio
                audio_segment = AudioSegment.from_raw(
                    io.BytesIO(current_audio_chunk),
                    sample_width=2, # 16-bit
                    frame_rate=16000,
                    channels=1
                )
                
                # Export to WAV in memory
                wav_io = io.BytesIO()
                audio_segment.export(wav_io, format="wav")
                wav_io.seek(0)
                
                with sr.AudioFile(wav_io) as source:
                    audio_data = recognizer.record(source) # Read the entire WAV file

                try:
                    # Use Google Web Speech API (online)
                    text = recognizer.recognize_google(audio_data)
                    print(f"Backend Recognized (Google): {text}")
                    with latest_transcription_lock:
                        latest_transcription_result = {"status": "success", "text": text}
                except sr.UnknownValueError:
                    # print("Google Speech Recognition could not understand audio")
                    pass # It's okay if it doesn't understand small chunks
                except sr.RequestError as e:
                    print(f"Could not request results from Google Speech Recognition service; {e}")
                except CouldntDecodeError as e:
                    print(f"pydub could not decode audio chunk: {e}")
                except Exception as e:
                    print(f"An unexpected error occurred during recognition: {e}")
            else:
                time.sleep(0.05) # Small sleep if no chunks or online mode is off

        except Exception as e:
            print(f"Error in transcription worker: {e}")
            time.sleep(1) # Sleep to prevent tight loop on persistent errors

# --- Main Execution Block ---
if __name__ == "__main__":
    # Start the Flask application in a separate thread
    flask_thread = threading.Thread(target=lambda: app.run(host='0.0.0.0', port=5000, debug=False, use_reloader=False), daemon=True)
    flask_thread.start()

    # Start the speech recognition worker in a separate thread
    worker_thread = threading.Thread(target=speech_recognition_worker, daemon=True)
    worker_thread.start()

    # Keep the main thread alive to allow daemon threads to run
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Stopping backend.")
    finally:
        # No explicit join needed for daemon threads on exit, but good practice for clean shutdown
        pass
