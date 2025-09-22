#ifndef KEYBOARD_BUTTON_H
#define KEYBOARD_BUTTON_H

#include <gtkmm/button.h>
#include <glibmm/ustring.h>
#include <sigc++/sigc++.h>
#include <iostream> // For DEBUG output in inline destructor

// Forward declaration of Keyboard class to avoid circular includes
class Keyboard;

class KeyboardButton : public Gtk::Button {
public:
    // Constructor: Takes the label for the button and a pointer to the parent Keyboard
    KeyboardButton(const Glib::ustring& label, Keyboard* parent_keyboard);

    // FIX: Define destructor inline in the header to ensure vtable generation
    // Removed debug print to avoid Gtk-CRITICAL warnings during shutdown
    virtual ~KeyboardButton() {
        // No specific resources to free here beyond what Gtk::Button handles
        // The parent Keyboard manages the lifetime of KeyboardButton objects via Gtk::manage()
        // std::cout << "DEBUG: KeyboardButton DESTROYED: " << get_label() << " (Address: " << this << ")" << std::endl;
    }

    // Signal definition for when this button is pressed
    using type_signal_keypress = sigc::signal<void, const Glib::ustring&>;
    type_signal_keypress signal_keypress();

    // Method to update the button's label based on CAPS lock state
    void update_label_for_caps(bool caps_active);

    // NEW: Getter for the original label
    Glib::ustring get_original_label() const;

protected:
    // Override the default Gtk::Button::on_clicked handler
    void on_clicked() override;

private:
    Keyboard* m_parent_keyboard; // Pointer to the parent Keyboard instance
    type_signal_keypress m_signal_keypress;
    Glib::ustring m_original_label; // NEW: Store the original label of the button
};

#endif // KEYBOARD_BUTTON_H
