#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <gtkmm/grid.h>         // For Gtk::Grid base class
#include <glibmm/ustring.h>     // For Glib::ustring
#include <sigc++/sigc++.h>      // For signals (sigc::signal, sigc::mem_fun)
#include <memory>               // For std::unique_ptr
#include <chrono>               // For std::chrono::steady_clock, std::chrono::milliseconds
#include <map>                  // For std::map to track modifier states and FN mappings

// --- X11 headers for KeySym and Display types ---
#include <X11/Xlib.h>          // For Display* type
#include <X11/keysym.h>         // For KeySym type
#include <X11/Xutil.h>          // For KeyCode, XKeysymToKeycode, XStringToKeysym (needed for send_char_globally)
#include <X11/XF86keysym.h>     // For multimedia and special function keys (though we'll use fewer now)

// --- NEW: Include glibmm/main.h for Glib::signal_idle() and Glib::signal_timeout() ---
#include <glibmm/main.h>

// --- NEW: Include SpeechToTextService header ---
#include "speechtotextservice.h"
#include "keyboardbutton.h" // Include KeyboardButton for the pointer declarations

class Keyboard : public Gtk::Grid {
public:
    Keyboard();
    virtual ~Keyboard();

    // Global X11 input sending
    void send_global_key_event(KeySym keysym, bool is_press);
    // New function to send a key with currently active modifiers
    void send_key_with_active_modifiers(KeySym base_keysym, bool needs_shift_for_base_char = false);

    // Signal definitions
    using type_signal_input = sigc::signal<void, const std::string&>;
    using type_signal_upper = sigc::signal<void, bool>; // Signal to update button labels for CAPS
    using type_signal_hide_show = sigc::signal<void>; // This will now trigger minimize/restore
    using type_signal_quit_app = sigc::signal<void>;

    type_signal_input signal_input();
    type_signal_upper signal_upper();
    type_signal_hide_show signal_hide_show();
    type_signal_quit_app signal_quit_app();

    // Button adding helpers
    void add_button(const Glib::ustring& label, int row, int col, int width = 1, int height = 1);
    KeyboardButton* add_button_and_return_ptr(const Glib::ustring& label, int row, int col, int width = 1, int height = 1);

    // Handle button presses from KeyboardButton
    void handle_button_press(const Glib::ustring& label);

    // Handle transcribed text from STT service
    void on_transcribed_text(const std::string& text);

    // Helper to apply CAPS state to all alpha buttons
    void apply_caps_state_to_buttons();

    // Helper to apply visual state to modifier buttons
    void update_modifier_button_visuals(const Glib::ustring& label, bool is_active);

    // NEW: Function to update labels of FN-shifted keys
    void update_fn_key_labels();


    // Member variables that rely on X11 types
    Display* m_x_display;
    bool m_xtest_available;
    
    bool m_caps_active; // Track CAPS lock state (visual toggle, also affects shift for letters)

    // Modifier key states
    bool m_shift_active; // Tracks if SHIFT button is currently "held down"
    bool m_ctrl_active;  // Tracks if CTRL button is currently "held down"
    bool m_alt_active;   // Tracks if ALT button is currently "held down"
    bool m_altgr_active; // Tracks if ALTGR button is currently "held down" (XK_ISO_Level3_Shift)
    bool m_fn_active;    // Tracks if FN button is currently "held down" (toggled)
    bool m_compose_active; // NEW: Tracks if COMPOSE button is currently "held down" (toggled)

    type_signal_input m_signal_input;
    type_signal_upper m_signal_upper;
    type_signal_hide_show m_signal_hide_show;
    type_signal_quit_app m_signal_quit_app;

    // SpeechToTextService instance
    std::unique_ptr<SpeechToTextService> m_stt_service;

    // Members for MIC button blinking
    KeyboardButton* m_mic_button; // Pointer to the actual MIC button
    sigc::connection m_mic_blink_connection; // Connection for the Glib::signal_timeout
    bool on_mic_button_blink_timeout(); // Timeout handler for blinking

    // Debounce for MIC button
    std::chrono::steady_clock::time_point m_last_mic_click_time;
    const std::chrono::milliseconds m_mic_debounce_interval = std::chrono::milliseconds(500); // 500ms debounce

    // Keep track of all KeyboardButton instances to update their labels/styles
    std::vector<KeyboardButton*> m_buttons; // Store pointers to all buttons

    // NEW: Map to store FN-shifted key labels and their corresponding KeySyms
    // Key: original button label (e.g., "BACK", "↑")
    // Value: pair of (FN-shifted label, FN-shifted KeySym)
    std::map<Glib::ustring, std::pair<Glib::ustring, KeySym>> m_fn_key_map;

private:
    void init_x_display_and_xtest();
    void build_alphabetic_layout(); // New function for fixed layout
};

#endif // KEYBOARD_H
