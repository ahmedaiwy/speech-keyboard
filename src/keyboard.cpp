#include "keyboard.h"
#include "keyboardbutton.h"
#include <iostream>
#include <string>
#include <cctype>
#include <vector>
#include <thread>
#include <chrono>
#include <glibmm/main.h> // For Glib::signal_timeout
#include <algorithm> // For std::find

// X11 headers for global input
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/Xutil.h>

std::string keysym_to_string_local(KeySym ks) {
    char *name = XKeysymToString(ks);
    if (name) {
        return name;
    }
    return "UNKNOWN_KEYSYM";
}

const std::string VOSK_MODEL_PATH = "/home/android/dev/gtkmm-virtual-keyboard/vosk-linux-x86_64-0.3.45/model";

// --- Keyboard Class Implementation ---
Keyboard::Keyboard()
    : m_x_display(nullptr), m_xtest_available(false),
      m_caps_active(false), // Initialize CAPS lock state to false
      m_shift_active(false), // Initialize modifier states
      m_ctrl_active(false),
      m_alt_active(false),
      m_altgr_active(false),
      m_stt_service(nullptr), m_mic_button(nullptr),
      m_last_mic_click_time(std::chrono::steady_clock::now()) // Initialize debounce timer
{
    std::cout << "DEBUG: Keyboard constructor called." << std::endl;
    init_x_display_and_xtest();

    set_row_spacing(4);
    set_column_spacing(4);
    set_margin_bottom(10);
    set_margin_top(10);
    set_margin_left(10);
    set_margin_right(10);

    // Initialize SpeechToTextService with a callback to this keyboard's on_transcribed_text method
    m_stt_service = std::make_unique<SpeechToTextService>(
        sigc::mem_fun(this, &Keyboard::on_transcribed_text)
    );

    // Attempt to initialize STT service
    if (!m_stt_service->init(VOSK_MODEL_PATH)) {
        std::cerr << "ERROR: Failed to initialize SpeechToTextService. Offline transcription may not work." << std::endl;
    }

    // Build the fixed alphabetic layout directly
    build_alphabetic_layout();
}

Keyboard::~Keyboard() {
    std::cout << "DEBUG: Keyboard destructor called." << std::endl;
    if (m_x_display) {
        XCloseDisplay(m_x_display);
        std::cout << "DEBUG: X display closed." << std::endl;
    }
    // The unique_ptr m_stt_service will automatically be destructed here,
    // which will stop its threads and clean up resources.
}

void Keyboard::init_x_display_and_xtest() {
    m_x_display = XOpenDisplay(nullptr);
    if (!m_x_display) {
        std::cerr << "ERROR: Could not open X display. Global key events will not work." << std::endl;
        m_xtest_available = false;
        return;
    }

    int major_opcode, first_event, first_error;
    if (!XQueryExtension(m_x_display, "XTEST", &major_opcode, &first_event, &first_error)) {
        std::cerr << "WARNING: XTest extension not available. Global key events may not work as expected." << std::endl;
        m_xtest_available = false;
    } else {
        m_xtest_available = true;
        std::cout << "DEBUG: XTest extension available." << std::endl;
    }
}

void Keyboard::send_global_key_event(KeySym keysym, bool is_press) {
    if (!m_xtest_available || !m_x_display) {
        std::cerr << "ERROR: Cannot send global key event - XTest not available or display not open." << std::endl;
        return;
    }

    // Convert KeySym to KeyCode
    KeyCode keycode = XKeysymToKeycode(m_x_display, keysym);
    if (keycode == 0) {
        std::cerr << "WARNING: No KeyCode for KeySym: " << keysym_to_string_local(keysym) << std::endl;
        return;
    }

    // Send the event
    XTestFakeKeyEvent(m_x_display, keycode, is_press, CurrentTime);
    XFlush(m_x_display); // Ensure the event is sent immediately
    std::cout << "DEBUG: Sent global key event: " << keysym_to_string_local(keysym) << " (KeyCode: " << (int)keycode << "), " << (is_press ? "Press" : "Release") << std::endl;
}

void Keyboard::send_key_with_active_modifiers(KeySym base_keysym, bool needs_shift_for_base_char) {
    // Press active modifiers
    // Note: XK_Shift_L is handled by needs_shift_for_base_char and m_shift_active
    if (m_ctrl_active) {
        send_global_key_event(XK_Control_L, true);
    }
    if (m_alt_active) {
        send_global_key_event(XK_Alt_L, true);
    }
    if (m_altgr_active) {
        send_global_key_event(XK_ISO_Level3_Shift, true);
    }
    // Handle shift if required by the base character or if SHIFT button is active
    if (needs_shift_for_base_char || m_shift_active) {
        send_global_key_event(XK_Shift_L, true);
    }

    // Send the base key
    send_global_key_event(base_keysym, true);
    send_global_key_event(base_keysym, false);

    // Release all modifiers that were pressed for this sequence (reverse order is safer)
    if (needs_shift_for_base_char || m_shift_active) {
        send_global_key_event(XK_Shift_L, false);
    }
    if (m_altgr_active) {
        send_global_key_event(XK_ISO_Level3_Shift, false);
    }
    if (m_alt_active) {
        send_global_key_event(XK_Alt_L, false);
    }
    if (m_ctrl_active) {
        send_global_key_event(XK_Control_L, false);
    }
}


void Keyboard::handle_button_press(const Glib::ustring& label) {
    std::cout << "DEBUG: Keyboard received key label: " << label << std::endl;

    if (label == "SHIFT") {
        m_shift_active = !m_shift_active;
        update_modifier_button_visuals(label, m_shift_active);
        // Update alpha keys based on new shift state (if CAPS is off)
        if (!m_caps_active) {
            apply_caps_state_to_buttons();
        }
    } else if (label == "CAPS") {
        m_caps_active = !m_caps_active;
        apply_caps_state_to_buttons(); // This updates alpha key labels and CAPS button visual
        update_modifier_button_visuals(label, m_caps_active); // Ensure CAPS button visual is correct
    } else if (label == "CTRL") {
        m_ctrl_active = !m_ctrl_active;
        update_modifier_button_visuals(label, m_ctrl_active);
    } else if (label == "ALT") {
        m_alt_active = !m_alt_active;
        update_modifier_button_visuals(label, m_alt_active);
    } else if (label == "ALTGR") {
        m_altgr_active = !m_altgr_active;
        update_modifier_button_visuals(label, m_altgr_active);
    } else if (label == "COMPOSE") { // NEW: Handle COMPOSE button
        std::cout << "DEBUG: Compose button pressed. Sending XK_Multi_key." << std::endl;
        send_global_key_event(XK_Multi_key, true); // Press Compose key
        send_global_key_event(XK_Multi_key, false); // Release Compose key
        // Compose key is typically momentary, not a toggle. No internal state needed.
    }
    else if (label == "BACK") {
        send_key_with_active_modifiers(XK_BackSpace);
    } else if (label == "SPACE") {
        send_key_with_active_modifiers(XK_space);
    } else if (label == "ENTER") {
        send_key_with_active_modifiers(XK_Return);
    } else if (label == "HIDE") {
        m_signal_hide_show.emit(); // Emit signal to hide/show window
    } else if (label == "KILL") {
        m_signal_quit_app.emit(); // Emit signal to quit application
    } else if (label == "FN") { // Handle FN key - no global character send
        std::cout << "DEBUG: FN key pressed. No global action for this key in current configuration." << std::endl;
        // No global key event sent for FN
    } else if (label == "←") {
        send_key_with_active_modifiers(XK_Left);
    } else if (label == "↑") {
        send_key_with_active_modifiers(XK_Up);
    } else if (label == "↓") {
        send_key_with_active_modifiers(XK_Down);
    } else if (label == "→") {
        send_key_with_active_modifiers(XK_Right);
    } else if (label.find("F") == 0 && label.length() > 1 && std::isdigit(label[1])) { // F1-F12 keys
        int f_num = std::stoi(label.substr(1));
        if (f_num >= 1 && f_num <= 12) {
            send_key_with_active_modifiers(XK_F1 + (f_num - 1));
        } else {
            std::cerr << "WARNING: Unhandled F-key: " << label << std::endl;
        }
    }
    else if (label == "🎙️") { // MIC button
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_mic_click_time);
        if (duration < m_mic_debounce_interval) {
            std::cout << "DEBUG: MIC button debounced. Ignoring rapid click." << std::endl;
            return; // Ignore the click if it's too soon
        }
        m_last_mic_click_time = now; // Update last click time

        std::cout << "DEBUG: Microphone button pressed. Toggling STT listening." << std::endl;
        if (m_stt_service->is_listening()) {
            m_stt_service->stop_listening();
            // Stop blinking
            if (m_mic_blink_connection.connected()) {
                m_mic_blink_connection.disconnect();
            }
            m_mic_button->get_style_context()->remove_class("mic-active");
            m_mic_button->get_style_context()->remove_class("mic-blinking"); // Ensure blinking class is removed
            m_mic_button->set_label("🎙️"); // Reset label if it was changed
        } else {
            // Start listening
            m_stt_service->start_listening();
            m_mic_button->get_style_context()->add_class("mic-active");
            // Start blinking
            m_mic_blink_connection = Glib::signal_timeout().connect(
                sigc::mem_fun(this, &Keyboard::on_mic_button_blink_timeout), 500
            );
        }
    }
    else {
        // For regular character buttons (alphabetic, numeric, common symbols)
        KeySym keysym = 0;
        bool needs_shift_for_char = false; // This is for characters that inherently require shift (e.g., '!', '@')

        if (label.length() == 1) {
            char c = label[0];
            if (std::isalpha(c)) {
                keysym = XStringToKeysym(Glib::ustring(1, static_cast<char>(std::tolower(c))).c_str());
                // If CAPS is active, or SHIFT button is active, treat as needing shift for this character
                if (m_caps_active || m_shift_active) {
                    needs_shift_for_char = true;
                }
            } else if (std::isdigit(c)) {
                keysym = XStringToKeysym(Glib::ustring(1, c).c_str());
            } else {
                // Handle symbols that might require shift or different keysyms
                switch (c) {
                    case '!': keysym = XK_exclam; needs_shift_for_char = true; break;
                    case '@': keysym = XK_at; needs_shift_for_char = true; break;
                    case '#': keysym = XK_numbersign; needs_shift_for_char = true; break;
                    case '$': keysym = XK_dollar; needs_shift_for_char = true; break;
                    case '%': keysym = XK_percent; needs_shift_for_char = true; break;
                    case '^': keysym = XK_asciicircum; needs_shift_for_char = true; break;
                    case '&': keysym = XK_ampersand; needs_shift_for_char = true; break;
                    case '*': keysym = XK_asterisk; needs_shift_for_char = true; break;
                    case '(': keysym = XK_parenleft; needs_shift_for_char = true; break;
                    case ')': keysym = XK_parenright; needs_shift_for_char = true; break;
                    case '-': keysym = XK_minus; break;
                    case '_': keysym = XK_underscore; needs_shift_for_char = true; break;
                    case '=': keysym = XK_equal; break;
                    case '+': keysym = XK_plus; needs_shift_for_char = true; break;
                    case '[': keysym = XK_bracketleft; break;
                    case '{': keysym = XK_braceleft; needs_shift_for_char = true; break;
                    case ']': keysym = XK_bracketright; break;
                    case '}': keysym = XK_braceright; needs_shift_for_char = true; break;
                    case ';': keysym = XK_semicolon; break;
                    case ':': keysym = XK_colon; needs_shift_for_char = true; break;
                    case '\'': keysym = XK_apostrophe; break;
                    case '\"': keysym = XK_quotedbl; needs_shift_for_char = true; break;
                    case '`': keysym = XK_grave; break;
                    case '~': keysym = XK_asciitilde; needs_shift_for_char = true; break;
                    case ',': keysym = XK_comma; break;
                    case '<': keysym = XK_less; needs_shift_for_char = true; break;
                    case '.': keysym = XK_period; break;
                    case '>': keysym = XK_greater; needs_shift_for_char = true; break;
                    case '/': keysym = XK_slash; break;
                    case '?': keysym = XK_question; needs_shift_for_char = true; break;
                    case '\\': keysym = XK_backslash; break;
                    case '|': keysym = XK_bar; needs_shift_for_char = true; break;
                    default:
                        std::cerr << "WARNING: Unhandled character for global send: '" << c << "'" << std::endl;
                        return;
                }
            }

            if (keysym != 0) {
                send_key_with_active_modifiers(keysym, needs_shift_for_char);
            } else {
                std::cerr << "WARNING: Could not determine KeySym for character: '" << c << "'" << std::endl;
            }
        } else {
             std::cerr << "WARNING: Button label too long or unsupported for direct global send: " << label << std::endl;
        }
    }
}

void Keyboard::on_transcribed_text(const std::string& text) {
    Glib::signal_idle().connect([this, text]() -> bool {
        if (!text.empty() && text != " ") {
            std::cout << "DEBUG: Transcribed text received: '" << text << "'" << std::endl;
            
            // Ensure all modifier states are off before sending transcribed text
            // This prevents transcribed text from being sent with active Ctrl/Alt etc.
            // We store their original state to restore them after transcription.
            bool was_shift_active = m_shift_active;
            bool was_ctrl_active = m_ctrl_active;
            bool was_alt_active = m_alt_active;
            bool was_altgr_active = m_altgr_active;

            // Deactivate modifiers for STT output
            if (m_shift_active) { m_shift_active = false; update_modifier_button_visuals("SHIFT", false); }
            if (m_ctrl_active) { m_ctrl_active = false; update_modifier_button_visuals("CTRL", false); }
            if (m_alt_active) { m_alt_active = false; update_modifier_button_visuals("ALT", false); }
            if (m_altgr_active) { m_altgr_active = false; update_modifier_button_visuals("ALTGR", false); }
            // CAPS state is a visual toggle on the keyboard, not a momentary modifier for STT output

            for (char c : text) {
                KeySym keysym = 0;
                bool needs_shift_for_char = false; // This is for characters inherently needing shift (e.g., 'A', '!')

                if (std::isupper(c)) {
                    needs_shift_for_char = true;
                    keysym = XStringToKeysym(Glib::ustring(1, static_cast<char>(std::tolower(c))).c_str());
                } else if (std::islower(c) || std::isdigit(c)) {
                    keysym = XStringToKeysym(Glib::ustring(1, c).c_str());
                } else {
                    // Handle symbols that might require shift or different keysyms
                    switch (c) {
                        case '!': keysym = XK_exclam; needs_shift_for_char = true; break;
                        case '@': keysym = XK_at; needs_shift_for_char = true; break;
                        case '#': keysym = XK_numbersign; needs_shift_for_char = true; break;
                        case '$': keysym = XK_dollar; needs_shift_for_char = true; break;
                        case '%': keysym = XK_percent; needs_shift_for_char = true; break;
                        case '^': keysym = XK_asciicircum; needs_shift_for_char = true; break;
                        case '&': keysym = XK_ampersand; needs_shift_for_char = true; break;
                        case '*': keysym = XK_asterisk; needs_shift_for_char = true; break;
                        case '(': keysym = XK_parenleft; needs_shift_for_char = true; break;
                        case ')': keysym = XK_parenright; needs_shift_for_char = true; break;
                        case '-': keysym = XK_minus; break;
                        case '_': keysym = XK_underscore; needs_shift_for_char = true; break;
                        case '=': keysym = XK_equal; break;
                        case '+': keysym = XK_plus; needs_shift_for_char = true; break;
                        case '[': keysym = XK_bracketleft; break;
                        case '{': keysym = XK_braceleft; needs_shift_for_char = true; break;
                        case ']': keysym = XK_bracketright; break;
                        case '}': keysym = XK_braceright; needs_shift_for_char = true; break;
                        case ';': keysym = XK_semicolon; break;
                        case ':': keysym = XK_colon; needs_shift_for_char = true; break;
                        case '\'': keysym = XK_apostrophe; break;
                        case '\"': keysym = XK_quotedbl; needs_shift_for_char = true; break;
                        case '`': keysym = XK_grave; break;
                        case '~': keysym = XK_asciitilde; needs_shift_for_char = true; break;
                        case ',': keysym = XK_comma; break;
                        case '<': keysym = XK_less; needs_shift_for_char = true; break;
                        case '.': keysym = XK_period; break;
                        case '>': keysym = XK_greater; needs_shift_for_char = true; break;
                        case '/': keysym = XK_slash; break;
                        case '?': keysym = XK_question; needs_shift_for_char = true; break;
                        case '\\': keysym = XK_backslash; break;
                        case '|': keysym = XK_bar; needs_shift_for_char = true; break;
                        case '\b': keysym = XK_BackSpace; break;
                        case 127: keysym = XK_Delete; break;
                        case ' ': keysym = XK_space; break;
                        case '\t': keysym = XK_Tab; break;
                        case '\n': keysym = XK_Return; break;
                        default:
                            std::cerr << "WARNING: Unhandled transcribed character: '" << c << "'" << std::endl;
                            keysym = 0; // Reset keysym if unhandled
                            break;
                    }
                }

                if (keysym != 0) {
                    // Temporarily press shift if needed for this character
                    if (needs_shift_for_char) {
                        send_global_key_event(XK_Shift_L, true);
                    }
                    send_global_key_event(keysym, true);
                    send_global_key_event(keysym, false);
                    if (needs_shift_for_char) {
                        send_global_key_event(XK_Shift_L, false);
                    }
                }
            }
            // Add a space after transcription for readability, if it's a new word/phrase
            send_global_key_event(XK_space, true);
            send_global_key_event(XK_space, false);

            // Restore modifier states after sending transcribed text
            if (was_shift_active) { m_shift_active = true; update_modifier_button_visuals("SHIFT", true); }
            if (was_ctrl_active) { m_ctrl_active = true; update_modifier_button_visuals("CTRL", true); }
            if (was_alt_active) { m_alt_active = true; update_modifier_button_visuals("ALT", true); }
            if (was_altgr_active) { m_altgr_active = true; update_modifier_button_visuals("ALTGR", true); }
        }
        return false; // Return false to disconnect the handler after one call
    });
}

void Keyboard::build_alphabetic_layout() {
    std::cout << "DEBUG: build_alphabetic_layout called." << std::endl;
    // Disconnect MIC button blink connection if active
    if (m_mic_blink_connection.connected()) {
        m_mic_blink_connection.disconnect();
    }
    // Remove all existing buttons and clear the vector
    for (KeyboardButton* button : m_buttons) {
        remove(*button);
    }
    m_buttons.clear(); // Clear the vector
    // Reset pointers to special buttons to avoid dangling pointers
    m_mic_button = nullptr;

    int row = 0;
    int col = 0;

    // Row 0: Function keys, Hide, Kill (Total 15 columns)
    add_button("ESC", row, col++);
    add_button("F1", row, col++);
    add_button("F2", row, col++);
    add_button("F3", row, col++);
    add_button("F4", row, col++);
    add_button("F5", row, col++);
    add_button("F6", row, col++);
    add_button("F7", row, col++);
    add_button("F8", row, col++);
    add_button("F9", row, col++);
    add_button("F10", row, col++);
    add_button("F11", row, col++);
    add_button("F12", row, col++);
    add_button("HIDE", row, col++);
    add_button("KILL", row, col++);

    row++; col = 0; // Next row

    // Row 1: Numbers and symbols (Total 15 columns, BACK is 2 units wide)
    m_buttons.push_back(add_button_and_return_ptr("`", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("1", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("2", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("3", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("4", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("5", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("6", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("7", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("8", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("9", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("0", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("-", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("=", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("BACK", row, col, 2)); col += 2; // Backspace is 2 units wide

    row++; col = 0; // Next row

    // Row 2: QWERTY (Total 14 columns, TAB and \ are 1 unit wide for simplicity)
    m_buttons.push_back(add_button_and_return_ptr("TAB", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("q", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("w", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("e", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("r", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("t", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("y", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("u", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("i", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("o", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("p", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("[", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("]", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("\\", row, col++));

    row++; col = 0; // Next row

    // Row 3: ASDF (Total 15 columns, CAPS and ENTER are 2 units wide)
    m_buttons.push_back(add_button_and_return_ptr("CAPS", row, col, 2)); col += 2;
    m_buttons.push_back(add_button_and_return_ptr("a", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("s", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("d", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("f", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("g", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("h", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("j", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("k", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("l", row, col++));
    m_buttons.push_back(add_button_and_return_ptr(";", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("'", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("ENTER", row, col, 2)); col += 2;

    row++; col = 0; // Next row

    // Row 4: ZXCV (Total 14 columns, SHIFT is 2 units wide on both sides)
    m_buttons.push_back(add_button_and_return_ptr("SHIFT", row, col, 2)); col += 2;
    m_buttons.push_back(add_button_and_return_ptr("z", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("x", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("c", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("v", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("b", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("n", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("m", row, col++));
    m_buttons.push_back(add_button_and_return_ptr(",", row, col++));
    m_buttons.push_back(add_button_and_return_ptr(".", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("/", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("SHIFT", row, col, 2)); col += 2;

    row++; col = 0; // Next row

    // Row 5: Bottom row (Total 15 columns, SPACE is 6 units wide)
    m_buttons.push_back(add_button_and_return_ptr("CTRL", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("FN", row, col++)); // Simple FN key, no layout switch
    m_buttons.push_back(add_button_and_return_ptr("ALT", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("SPACE", row, col, 6)); col += 6;
    m_buttons.push_back(add_button_and_return_ptr("ALTGR", row, col++)); // AltGr button
    m_buttons.push_back(add_button_and_return_ptr("COMPOSE", row, col++)); // NEW: Compose button
    m_buttons.push_back(add_button_and_return_ptr("←", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("↑", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("↓", row, col++));
    m_buttons.push_back(add_button_and_return_ptr("→", row, col++));
    
    // MIC button
    m_mic_button = add_button_and_return_ptr("🎙️", row, col++);
    m_mic_button->get_style_context()->add_class("mic-button");

    show_all_children(); // Ensure all newly added buttons are shown
}

void Keyboard::add_button(const Glib::ustring& label, int row, int col, int width, int height) {
    KeyboardButton* button = Gtk::manage(new KeyboardButton(label, this));
    attach(*button, col, row, width, height);
    button->signal_keypress().connect(sigc::mem_fun(this, &Keyboard::handle_button_press));
    // Add all buttons to m_buttons vector for CAPS state management
    m_buttons.push_back(button);
}

KeyboardButton* Keyboard::add_button_and_return_ptr(const Glib::ustring& label, int row, int col, int width, int height) {
    KeyboardButton* button = Gtk::manage(new KeyboardButton(label, this));
    attach(*button, col, row, width, height);
    button->signal_keypress().connect(sigc::mem_fun(this, &Keyboard::handle_button_press));
    m_buttons.push_back(button); // Add to vector
    return button; // Return the pointer
}

void Keyboard::apply_caps_state_to_buttons() {
    std::cout << "DEBUG: Applying CAPS state. Current caps_active: " << (m_caps_active ? "true" : "false") << std::endl;
    for (KeyboardButton* button : m_buttons) {
        Glib::ustring label = button->get_label();
        if (label.length() == 1 && std::isalpha(label[0])) {
            if (m_caps_active) {
                button->set_label(Glib::ustring(1, static_cast<char>(std::toupper(label[0]))));
            } else {
                button->set_label(Glib::ustring(1, static_cast<char>(std::tolower(label[0]))));
            }
        } else if (label == "CAPS") { // Apply visual state to CAPS button
            if (m_caps_active) {
                button->get_style_context()->add_class("toggle-active");
            } else {
                button->get_style_context()->remove_class("toggle-active");
            }
        }
    }
    // Also update SHIFT button if it's currently active, as CAPS affects its behavior
    // This is handled by update_modifier_button_visuals for SHIFT itself.
}

void Keyboard::update_modifier_button_visuals(const Glib::ustring& label, bool is_active) {
    for (KeyboardButton* button : m_buttons) {
        if (button->get_label() == label) {
            Glib::RefPtr<Gtk::StyleContext> context = button->get_style_context();
            if (is_active) {
                context->add_class("toggle-active");
            } else {
                context->remove_class("toggle-active");
            }
            // If SHIFT state changes, and CAPS is not active, update alpha keys
            if (label == "SHIFT" && !m_caps_active) {
                apply_caps_state_to_buttons(); // Re-apply to change case of alpha keys
            }
            break; // Found the button, no need to continue
        }
    }
}

bool Keyboard::on_mic_button_blink_timeout() {
    if (m_mic_button) {
        // Toggle the 'mic-blinking' class
        if (m_mic_button->get_style_context()->has_class("mic-blinking")) {
            m_mic_button->get_style_context()->remove_class("mic-blinking");
        } else {
            m_mic_button->get_style_context()->add_class("mic-blinking");
        }
    }
    return m_stt_service->is_listening(); // Continue blinking if still listening
}

// Signal accessors (implementation)
Keyboard::type_signal_input Keyboard::signal_input() {
    return m_signal_input;
}

Keyboard::type_signal_upper Keyboard::signal_upper() {
    return m_signal_upper;
}

Keyboard::type_signal_hide_show Keyboard::signal_hide_show() {
    return m_signal_hide_show;
}

Keyboard::type_signal_quit_app Keyboard::signal_quit_app() {
    return m_signal_quit_app;
}
