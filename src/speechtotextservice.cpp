// speechtotextservice.cpp
// Implementation file for the SpeechToTextService class.
// This class manages speech-to-text functionality using the Vosk C++ API.
// It captures audio directly using ALSA and feeds it to Vosk for transcription.

#include "speechtotextservice.h"
#include <iostream>
#include <vector>
#include <stdexcept> // For std::runtime_error
#include <numeric>   // For std::accumulate (used for simple audio check)
#include <algorithm> // For std::all_of (used for simple audio check)
#include <cmath>     // For std::abs (used for simple audio check)
// Removed <queue> and <json-c/json.h> as they are not needed for offline-only
// Removed nlohmann/json as it's not needed for offline-only

// Include ALSA header for audio capture
#include <alsa/asoundlib.h>

// Include Vosk API header
#include <vosk_api.h>

// Ensure std::filesystem is available (C++17+) for path existence check
#include <filesystem>

// Removed CURL and Base64 related helpers

// --- SpeechToTextService Class Implementation ---

SpeechToTextService::SpeechToTextService(TranscribedTextCallback callback)
    : m_transcribed_text_callback(callback),
      m_model(nullptr),
      m_recognizer(nullptr),
      m_alsa_handle(nullptr),
      m_listening(false),
      m_should_run_audio_thread(false)
{
    std::cout << "SpeechToTextService: Constructor called." << std::endl;
}

SpeechToTextService::~SpeechToTextService() {
    std::cout << "SpeechToTextService: Destructor called." << std::endl;
    stop_listening(); // Ensure all threads are stopped and resources are freed
    
    // Clean up Vosk resources
    if (m_recognizer) {
        vosk_recognizer_free(m_recognizer);
        m_recognizer = nullptr;
    }
    if (m_model) {
        vosk_model_free(m_model);
        m_model = nullptr;
    }
    std::cout << "SpeechToTextService: Vosk resources freed." << std::endl;
}

bool SpeechToTextService::init(const std::string& model_path) {
    if (!std::filesystem::exists(model_path)) {
        std::cerr << "ERROR: Vosk model path does not exist: " << model_path << std::endl;
        return false;
    }

    vosk_set_log_level(-1); // Disable Vosk logging to console

    m_model = vosk_model_new(model_path.c_str());
    if (!m_model) {
        std::cerr << "ERROR: Failed to create Vosk model from path: " << model_path << std::endl;
        return false;
    }
    std::cout << "SpeechToTextService: Vosk model loaded successfully." << std::endl;

    // Recognizer is created when listening starts, as it's tied to audio format.
    return true;
}

bool SpeechToTextService::start_listening() {
    if (m_listening) {
        std::cout << "SpeechToTextService: Already listening." << std::endl;
        return true;
    }

    std::cout << "SpeechToTextService: Starting listening..." << std::endl;

    // Initialize ALSA capture
    if (!open_alsa_capture()) {
        std::cerr << "ERROR: Failed to open ALSA capture. Cannot start listening." << std::endl;
        return false;
    }

    // Create Vosk recognizer (needs to be done after ALSA setup to get sample rate)
    if (m_recognizer) {
        vosk_recognizer_free(m_recognizer); // Free old one if exists
    }
    m_recognizer = vosk_recognizer_new(m_model, 16000.0f); // Vosk expects float sample rate
    if (!m_recognizer) {
        std::cerr << "ERROR: Failed to create Vosk recognizer." << std::endl;
        close_alsa_capture();
        return false;
    }

    m_listening = true;
    m_should_run_audio_thread = true;
    m_audio_processing_thread = std::thread(&SpeechToTextService::audio_processing_loop, this);
    
    std::cout << "SpeechToTextService: Listening started." << std::endl;
    return true;
}

void SpeechToTextService::stop_listening() {
    if (!m_listening) {
        std::cout << "SpeechToTextService: Not currently listening." << std::endl;
        return;
    }

    std::cout << "SpeechToTextService: Stopping listening..." << std::endl;

    m_should_run_audio_thread = false;
    if (m_audio_processing_thread.joinable()) {
        m_audio_processing_thread.join();
        std::cout << "SpeechToTextService: Audio processing thread joined." << std::endl;
    }

    // Get final result from Vosk recognizer on stop
    if (m_recognizer) {
        char* final_result_cstr = const_cast<char*>(vosk_recognizer_final_result(m_recognizer));
        std::string final_result_json_str(final_result_cstr);
        std::cout << "DEBUG: Vosk final result JSON (on stop): " << final_result_json_str << std::endl;
        
        // Simple JSON parsing for "text" field
        // This is a basic parser. For robust parsing, consider a JSON library.
        size_t text_pos = final_result_json_str.find("\"text\" : \"");
        if (text_pos != std::string::npos) {
            text_pos += std::string("\"text\" : \"").length();
            size_t end_pos = final_result_json_str.find("\"", text_pos);
            if (end_pos != std::string::npos) {
                std::string text = final_result_json_str.substr(text_pos, end_pos - text_pos);
                if (!text.empty() && text != " ") {
                    m_transcribed_text_callback(text);
                }
            }
        }
    }

    close_alsa_capture();
    m_listening = false;
    std::cout << "SpeechToTextService: Listening stopped." << std::endl;
}

bool SpeechToTextService::is_listening() const {
    return m_listening;
}

bool SpeechToTextService::open_alsa_capture() {
    int err;
    std::string pcm_device = "default"; // Or "plughw:1,0" etc.

    // Open PCM device for recording
    if ((err = snd_pcm_open(&m_alsa_handle, pcm_device.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        std::cerr << "ERROR: Cannot open audio device " << pcm_device << ": " << snd_strerror(err) << std::endl;
        return false;
    }

    // Set PCM parameters
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_hw_params_alloca(&hw_params);
    snd_pcm_hw_params_any(m_alsa_handle, hw_params);

    // Set interleaved mode
    snd_pcm_hw_params_set_access(m_alsa_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);

    // Set sample format (Vosk expects 16-bit signed integers)
    snd_pcm_hw_params_set_format(m_alsa_handle, hw_params, SND_PCM_FORMAT_S16_LE);

    // Set channels (Vosk expects mono)
    unsigned int channels = 1;
    snd_pcm_hw_params_set_channels(m_alsa_handle, hw_params, channels);

    // Set sample rate
    unsigned int rate = 16000; // 16kHz
    snd_pcm_hw_params_set_rate_near(m_alsa_handle, hw_params, &rate, 0);

    // Set period size (buffer chunk size)
    snd_pcm_uframes_t period_size = 800; // About 50ms of 16kHz 16-bit mono audio (800 frames * 2 bytes/frame = 1600 bytes)
    snd_pcm_hw_params_set_period_size_near(m_alsa_handle, hw_params, &period_size, 0);

    // Set buffer size
    snd_pcm_uframes_t buffer_size = 4000; // 250ms buffer (4000 frames * 2 bytes/frame = 8000 bytes)
    snd_pcm_hw_params_set_buffer_size_near(m_alsa_handle, hw_params, &buffer_size);

    // Apply parameters
    if ((err = snd_pcm_hw_params(m_alsa_handle, hw_params)) < 0) {
        std::cerr << "ERROR: Cannot set parameters: " << snd_strerror(err) << std::endl;
        close_alsa_capture();
        return false;
    }

    // Get actual parameters (optional, for verification)
    unsigned int actual_rate;
    snd_pcm_hw_params_get_rate(hw_params, &actual_rate, 0);
    unsigned int actual_channels;
    snd_pcm_hw_params_get_channels(hw_params, &actual_channels);
    snd_pcm_uframes_t actual_buffer_size;
    snd_pcm_hw_params_get_buffer_size(hw_params, &actual_buffer_size);
    snd_pcm_uframes_t actual_period_size;
    snd_pcm_hw_params_get_period_size(hw_params, &actual_period_size, 0);

    std::cout << "SpeechToTextService: ALSA params - Requested Rate: 16000Hz, Actual Rate: " << actual_rate
              << "Hz, Requested Channels: 1, Actual Channels: " << actual_channels
              << ", Buffer Size (frames): " << actual_buffer_size
              << ", Period Size (frames): " << actual_period_size << std::endl;

    // Prepare PCM device
    if ((err = snd_pcm_prepare(m_alsa_handle)) < 0) {
        std::cerr << "ERROR: Cannot prepare audio interface for use: " << snd_strerror(err) << std::endl;
        close_alsa_capture();
        return false;
    }

    return true;
}

void SpeechToTextService::close_alsa_capture() {
    if (m_alsa_handle) {
        std::cout << "SpeechToTextService: Closing ALSA capture device." << std::endl;
        snd_pcm_close(m_alsa_handle);
        m_alsa_handle = nullptr;
        std::cout << "SpeechToTextService: ALSA capture device closed." << std::endl;
    }
}

void SpeechToTextService::audio_processing_loop() {
    const int buffer_size_bytes = 1600; // 800 frames * 2 bytes/frame (S16_LE)
    std::vector<char> buffer(buffer_size_bytes);
    int err;

    while (m_should_run_audio_thread) {
        if (!m_alsa_handle) {
            std::cerr << "ERROR: ALSA handle is null in audio_processing_loop." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        err = snd_pcm_readi(m_alsa_handle, buffer.data(), buffer_size_bytes / 2); // Read frames (buffer_size_bytes / 2)
        if (err == -EPIPE) {
            std::cerr << "WARNING: ALSA overrun occurred, attempting to recover." << std::endl;
            snd_pcm_prepare(m_alsa_handle);
            continue;
        } else if (err < 0) {
            std::cerr << "ERROR: Error from ALSA read: " << snd_strerror(err) << std::endl;
            break;
        } else if (err != buffer_size_bytes / 2) {
            std::cerr << "WARNING: Short read from ALSA, expected " << buffer_size_bytes / 2 << " frames, got " << err << std::endl;
        }

        if (m_recognizer) {
            int rec_res = vosk_recognizer_accept_waveform(m_recognizer, buffer.data(), buffer_size_bytes);
            
            // Log partial results for debugging, but DO NOT send them to callback for global typing
            char* partial_result_cstr = const_cast<char*>(vosk_recognizer_partial_result(m_recognizer));
            std::string partial_result_json_str(partial_result_cstr);
            // std::cout << "DEBUG: Vosk result JSON (Partial): " << partial_result_json_str << std::endl;

            if (rec_res == 1) { // Final result
                char* final_result_cstr = const_cast<char*>(vosk_recognizer_result(m_recognizer));
                std::string final_result_json_str(final_result_cstr);
                std::cout << "DEBUG: Vosk result JSON (Final): " << final_result_json_str << std::endl;

                // Simple JSON parsing for "text" field
                size_t text_pos = final_result_json_str.find("\"text\" : \"");
                if (text_pos != std::string::npos) {
                    text_pos += std::string("\"text\" : \"").length();
                    size_t end_pos = final_result_json_str.find("\"", text_pos);
                    if (end_pos != std::string::npos) {
                        std::string text = final_result_json_str.substr(text_pos, end_pos - text_pos);
                        if (!text.empty() && text != " ") {
                            m_transcribed_text_callback(text);
                        }
                    }
                }
            }
        }
    }
    std::cout << "SpeechToTextService: Audio processing loop finished." << std::endl;
}

