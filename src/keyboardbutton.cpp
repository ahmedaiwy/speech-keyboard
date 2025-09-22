// keyboardbutton.cpp
#include "keyboardbutton.h"
#include "keyboard.h" // Needed for parent Keyboard access
#include <iostream>   // For std::cerr
#include <cctype>     // For std::isalpha, std::toupper, std::tolower

// KeyboardButton implementation
KeyboardButton::KeyboardButton(const Glib::ustring& label, Keyboard* parent_keyboard):
      Gtk::Button(label), m_parent_keyboard(parent_keyboard), m_original_label(label) { // Store original label
  // Re-added expand properties for GTKmm 3.x to make buttons fill grid cells
  set_hexpand(true); // Allow horizontal expansion
  set_vexpand(true); // Allow vertical expansion
  set_halign(Gtk::ALIGN_FILL); // Fill available horizontal space
  set_valign(Gtk::ALIGN_FILL); // Fill available vertical space
  set_can_focus(false); // Buttons typically shouldn't take keyboard focus

  // Apply CSS classes based on button type for styling
  Glib::RefPtr<Gtk::StyleContext> context = get_style_context();
  if (label.length() == 1 && std::isalpha(label.c_str()[0])) {
      context->add_class("alpha-key");
  } else if (label == "SPACE") {
      context->add_class("space-key");
  } else if (label == "CAPS" || label == "SHIFT" || label == "ESC" || label == "TAB" || label == "Print" || label == "Scroll" || label == "Pause" || label == "Menu" || label == "ABC" || label == "123") {
      context->add_class("func-key");
  } else if (label == "LCtrl" || label == "LAlt" || label == "Alt Gr" || label == "COMPOSE") { // Specific classes for Ctrl, Alt, Alt Gr, Compose
      context->add_class("func-key"); // They are functional keys
      context->add_class("modifier-key"); // Add a specific modifier class if needed for styling
  } else if (label == "FN") { // Specific class for FN
      context->add_class("fn-key");
  } else if (label == "ENTER" || label == "BACK" || label == "KILL" || label == "HIDE" || label == "DEL") { // Add other func keys here, including DEL
      context->add_class("func-key");
  } else if (label == "←" || label == "↑" || label == "↓" || label == "→" || label == "Home" || label == "End" || label == "PgUp" || label == "PgDn") { // Arrow keys and FN-shifted navigation keys
      context->add_class("arrow-key");
  } else if (label == "#" || label == "-" || label == "=" || label == "[" || label == "]" || label == "\\" || label == ";" || label == "'" || label == "," || label == "." || label == "/" || label == "`" || label == "_" || label == "+") { // Common symbols including _ and +
      context->add_class("symbol-key");
  } else {
      context->add_class("symbol-key"); // Default for any unclassified single character or other symbols
  }

  // Connect the update_label_for_caps method to the Keyboard's signal_upper
  // This ensures alpha keys update their label when CAPS state changes globally
  if (label.length() == 1 && std::isalpha(label.c_str()[0]) && m_parent_keyboard) {
    m_parent_keyboard->signal_upper().connect(sigc::mem_fun(this, &KeyboardButton::update_label_for_caps));
  }

  if (!m_parent_keyboard) {
    std::cerr << "ERROR: KeyboardButton initialized with null parent_keyboard." << std::endl;
  }
}

// Destructor definition is now inline in keyboardbutton.h
// KeyboardButton::~KeyboardButton() {
//     // No specific resources to free here beyond what Gtk::Button handles
//     // The parent Keyboard manages the lifetime of KeyboardButton objects via Gtk::manage()
// }


void KeyboardButton::on_clicked(){
  // Handle CAPS, SHIFT, FN, LCtrl, RCtrl, LAlt, Alt Gr, COMPOSE toggle class directly here
  // as they are visual state changes of the button itself.
  if (get_original_label() == "CAPS" || get_original_label() == "SHIFT" || get_original_label() == "FN" ||
      get_original_label() == "LCtrl" || get_original_label() == "LAlt" ||
      get_original_label() == "Alt Gr" || get_original_label() == "COMPOSE") { // Removed RCtrl from here as it's now COMPOSE
      Glib::RefPtr<Gtk::StyleContext> context = get_style_context();
      if (context->has_class("toggle-active")) {
          context->remove_class("toggle-active");
      } else {
          context->add_class("toggle-active"); // Add if not active
      }
  }

  // Emit the custom signal. The Keyboard class is responsible for
  // connecting to this signal and handling the global key press.
  m_signal_keypress.emit(Gtk::Button::get_label());
}

KeyboardButton::type_signal_keypress KeyboardButton::signal_keypress(){
  return m_signal_keypress;
}

void KeyboardButton::update_label_for_caps(bool caps_active) {
    Glib::ustring current_label = get_label(); // Get the currently displayed label
    Glib::ustring original_label = get_original_label(); // Get the original label
    
    // Check if the current label is one of the FN-shifted labels
    bool is_fn_shifted_label_displayed = (current_label != original_label && m_parent_keyboard && m_parent_keyboard->m_fn_key_map.count(original_label));

    if (original_label.length() == 1 && std::isalpha(original_label.c_str()[0]) && !is_fn_shifted_label_displayed) {
        // Only modify single alpha character labels that are NOT currently FN-shifted
        char c = original_label.c_str()[0]; // Use original_label to determine if it's an alpha key
        if (caps_active) {
            set_label(Glib::ustring(1, static_cast<char>(std::toupper(c))));
        } else {
            set_label(Glib::ustring(1, static_cast<char>(std::tolower(c))));
        }
    }
}

Glib::ustring KeyboardButton::get_original_label() const {
    return m_original_label;
}
