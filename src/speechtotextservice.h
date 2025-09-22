// speechtotextservice.h
// Header file for the SpeechToTextService class.
// This class will manage speech-to-text functionality using the Vosk C++ API.

#ifndef SPEECH_TO_TEXT_SERVICE_H
#define SPEECH_TO_TEXT_SERVICE_H

#include <string>
#include <functional> // <--- REQUIRED for std::function used in TranscribedTextCallback
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <filesystem> // Include for std::filesystem::exists
// Removed <condition_variable> as it's not needed for offline-only

// Include ALSA header here, as it defines snd_pcm_t used in member declaration
#include <alsa/asoundlib.h> 

// Removed CURL includes as online mode is removed

// Forward declarations for Vosk C++ API classes
struct VoskModel;
struct VoskRecognizer;

// Define a callback function type that the service will use to send transcribed text
// back to the main application (e.g., your Gtkmm Keyboard).
using TranscribedTextCallback = std::function<void(const std::string&)>;

class SpeechToTextService {
public:
    // Constructor: Takes a callback function that will be invoked with transcribed text.
    SpeechToTextService(TranscribedTextCallback callback);

    // Destructor: Cleans up Vosk model and recognizer resources, stops threads.
    ~SpeechToTextService();

    // Initializes the Vosk model and recognizer
    bool init(const std::string& model_path);

    // Starts audio capture and speech recognition
    bool start_listening();

    // Stops audio capture and speech recognition
    void stop_listening();

    // Checks if the service is currently listening
    bool is_listening() const;

    // Removed set_online_mode and is_online_mode

private:
    TranscribedTextCallback m_transcribed_text_callback; // Callback for transcribed text

    VoskModel* m_model;       // Vosk model
    VoskRecognizer* m_recognizer; // Vosk recognizer

    snd_pcm_t* m_alsa_handle; // ALSA audio capture handle
    std::atomic<bool> m_listening; // Flag to control audio capture loop
    std::atomic<bool> m_should_run_audio_thread; // Flag to control audio processing thread
    std::thread m_audio_processing_thread; // Thread for audio capture and processing

    // Private methods for ALSA audio capture
    bool open_alsa_capture();
    void close_alsa_capture();

    // Audio processing loop (offline mode)
    void audio_processing_loop();

    // Removed all online mode related members and methods
};

#endif // SPEECH_TO_TEXT_SERVICE_H
