// cpp_alsa_audio_test_ubuntu.cpp - Skipping/Dropping Drain
// This program records 3 seconds of audio from the default microphone
// using ALSA (Advanced Linux Sound Architecture) and then plays it back.
//
// To compile: g++ cpp_alsa_audio_test_ubuntu.cpp -o cpp_alsa_audio_test_ubuntu -lasound
// To run: ./cpp_alsa_audio_test_ubuntu
//
// Requires libasound2-dev (sudo apt install libasound2-dev)

#include <iostream>
#include <vector>
#include <alsa/asoundlib.h> // ALSA library header
#include <algorithm> // For std::min

// Define audio parameters
#define SAMPLE_RATE 44100   // Samples per second (Hz) - Standard for good quality
#define CHANNELS 2          // Stereo - Common for microphones
#define FORMAT SND_PCM_FORMAT_S16_LE // 16-bit signed, little-endian - Standard PCM format
#define BUFFER_SIZE 4096    // Number of frames in a buffer chunk for ALSA
#define RECORD_SECONDS 3    // Duration of recording in seconds

int main() {
    long loops; // Loop counter for reading/writing frames
    int rc;     // Return code for ALSA functions
    int size;   // Size of one buffer chunk in bytes
    snd_pcm_t *handle; // PCM device handle (for both capture and playback)
    snd_pcm_hw_params_t *params; // Hardware parameters structure
    unsigned int val; // For setting sample rate (can be adjusted by ALSA)
    int dir;    // Direction for rate negotiation (0 for exact, 1 for nearest higher, -1 for nearest lower)
    snd_pcm_uframes_t frames; // Number of frames in a period (buffer chunk)
    
    // Vector to temporarily store audio data read/written in chunks
    std::vector<char> buffer_data;

    // --- 1. Open PCM device for recording (capture) ---
    std::cout << "Attempting to open ALSA PCM device for recording (default)..." << std::endl;
    rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0);
    if (rc < 0) {
        std::cerr << "ERROR: Unable to open PCM device for capture: " << snd_strerror(rc) << std::endl;
        std::cout << "HINT: Ensure your microphone is connected, recognized by Ubuntu, and not in exclusive use." << std::endl;
        std::cout << "Check 'pavucontrol' (PulseAudio Volume Control) settings for input devices." << std::endl;
        return 1;
    }
    std::cout << "PCM device opened successfully for capture." << std::endl;

    // --- 2. Allocate and initialize hardware parameters structure ---
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    // --- 3. Set hardware parameters for capture ---
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, FORMAT);
    snd_pcm_hw_params_set_channels(handle, params, CHANNELS);
    val = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);
    frames = BUFFER_SIZE;
    snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

    // --- 4. Write parameters to the driver ---
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
        std::cerr << "ERROR: Unable to set HW parameters for capture: " << snd_strerror(rc) << std::endl;
        snd_pcm_close(handle);
        return 1;
    }
    std::cout << "Hardware parameters set for capture." << std::endl;

    // --- 5. Prepare buffer for capture data and calculate total data to record ---
    size = frames * CHANNELS * (snd_pcm_format_width(FORMAT) / 8);
    buffer_data.resize(size);

    snd_pcm_hw_params_get_rate(params, &val, &dir);
    loops = (RECORD_SECONDS * val) / frames;

    std::cout << "Recording " << RECORD_SECONDS << " seconds of audio..." << std::endl;
    std::cout << "Actual Sample Rate: " << val << " Hz, Channels: " << CHANNELS << ", Format: S16_LE" << std::endl;
    std::cout << "Frames per buffer (period size): " << frames << ", Buffer size (bytes per chunk): " << size << std::endl;
    std::cout << "Total loops for recording: " << loops << std::endl;

    std::vector<char> recorded_audio;
    recorded_audio.reserve(loops * size);

    // --- 6. Start recording loop ---
    for (long i = 0; i < loops; ++i) {
        rc = snd_pcm_readi(handle, buffer_data.data(), frames);
        if (rc == -EPIPE) {
            std::cerr << "WARNING: ALSA Capture Overrun occurred, recovering..." << std::endl;
            snd_pcm_prepare(handle);
        } else if (rc < 0) {
            std::cerr << "ERROR: Error from readi: " << snd_strerror(rc) << std::endl;
            break;
        } else if (rc != (int)frames) {
            std::cerr << "WARNING: Short read, read " << rc << " frames instead of " << frames << std::endl;
        }
        recorded_audio.insert(recorded_audio.end(), buffer_data.begin(), buffer_data.end());
    }
    std::cout << "Recording complete. Total bytes recorded: " << recorded_audio.size() << std::endl;

    // --- NEW: Try dropping instead of draining, then close ---
    std::cout << "DEBUG: Attempting to drop pending capture frames..." << std::endl;
    rc = snd_pcm_drop(handle); // Drop any pending frames
    if (rc < 0) {
        std::cerr << "ERROR: Error dropping capture device: " << snd_strerror(rc) << std::endl;
    }
    std::cout << "DEBUG: Capture device frames dropped. rc=" << rc << std::endl;

    std::cout << "DEBUG: Attempting to close capture device immediately..." << std::endl;
    rc = snd_pcm_close(handle);
    if (rc < 0) {
        std::cerr << "ERROR: Error closing capture device: " << snd_strerror(rc) << std::endl;
    }
    std::cout << "DEBUG: Capture device closed. rc=" << rc << std::endl;


    // --- 8. Open PCM device for playback ---
    std::cout << "\nAttempting to open ALSA PCM device for playback (default)..." << std::endl;
    rc = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        std::cerr << "ERROR: Unable to open PCM device for playback: " << snd_strerror(rc) << std::endl;
        std::cout << "HINT: Ensure your speakers/headphones are connected and PulseAudio is running correctly." << std::endl;
        return 1;
    }
    std::cout << "PCM device opened successfully for playback." << std::endl;

    // --- 9. Allocate and initialize hardware parameters for playback ---
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    // --- 10. Set hardware parameters for playback (same as capture) ---
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, FORMAT);
    snd_pcm_hw_params_set_channels(handle, params, CHANNELS);
    val = SAMPLE_RATE;
    snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);
    frames = BUFFER_SIZE;
    snd_pcm_hw_params_set_period_size_near(handle, params, &frames, &dir);

    // --- 11. Write parameters to the driver for playback ---
    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
        std::cerr << "ERROR: Unable to set HW parameters for playback: " << snd_strerror(rc) << std::endl;
        snd_pcm_close(handle);
        return 1;
    }
    std::cout << "Hardware parameters set for playback." << std::endl;

    // --- 12. Playback loop ---
    std::cout << "Playing back recorded audio..." << std::endl;
    long total_frames_recorded = recorded_audio.size() / (CHANNELS * (snd_pcm_format_width(FORMAT) / 8));
    long frames_to_write = total_frames_recorded;
    char* playback_ptr = recorded_audio.data();

    while (frames_to_write > 0) {
        long current_frames = std::min((long)frames, frames_to_write);
        rc = snd_pcm_writei(handle, playback_ptr, current_frames);
        if (rc == -EPIPE) {
            std::cerr << "WARNING: ALSA Playback Underrun occurred, recovering..." << std::endl;
            snd_pcm_prepare(handle);
        } else if (rc < 0) {
            std::cerr << "ERROR: Error from writei: " << snd_strerror(rc) << std::endl;
            break;
        } else if (rc != current_frames) {
            std::cerr << "WARNING: Short write, wrote " << rc << " frames instead of " << current_frames << std::endl;
        }
        frames_to_write -= rc;
        playback_ptr += rc * CHANNELS * (snd_pcm_format_width(FORMAT) / 8);
    }
    std::cout << "Playback complete." << std::endl;

    // --- 13. Stop and close playback device ---
    snd_pcm_drain(handle);
    snd_pcm_close(handle);
    std::cout << "Playback device closed." << std::endl;

    return 0;
}

