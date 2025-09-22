#include <gtkmm/application.h>
#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h> // For the debug label
#include <iostream>
#include <thread>
#include <chrono>

// NEW: Added missing includes for Glib::signal_idle, Gtk::CssProvider, Gdk::WINDOW_TYPE_HINT_KEYBOARD
#include <glibmm/main.h>
#include <gtkmm/cssprovider.h>
#include <gdkmm/window.h> // For Gdk::WINDOW_TYPE_HINT_*

#include "keyboard.h" // Full include of Keyboard class definition

// Global pointer for debug label (for now, consider better alternatives for production)
Gtk::Label* g_debug_label = nullptr;

// Function to update the debug label from any thread
void update_debug_label(const Glib::ustring& text) {
    if (g_debug_label) {
        // Use Glib::signal_idle() to ensure the UI update happens on the main GTKmm thread
        Glib::signal_idle().connect_once([text]() {
            if (g_debug_label) {
                g_debug_label->set_text(text);
            }
        });
    }
}

int main(int argc, char* argv[]) {
    std::cout << "DEBUG: main() started." << std::endl;

    auto app = Gtk::Application::create(argc, argv, "org.gtkmm.example.VirtualKeyboard");

    // Load CSS
    try {
        auto provider = Gtk::CssProvider::create();
        provider->load_from_path("style.css");
        Glib::RefPtr<Gtk::StyleContext> context = Gtk::StyleContext::create();
        context->add_provider_for_screen(Gdk::Screen::get_default(), provider, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        std::cout << "DEBUG: Loaded CSS from: style.css" << std::endl;
    } catch (const Glib::Error& ex) {
        std::cerr << "ERROR: Failed to load CSS: " << ex.what() << std::endl;
    }

    Gtk::Window window;
    window.set_title("The Incredible Keyboard"); // Renamed title!
    window.set_default_size(800, 350); // Adjusted default size for the new layout
    window.set_position(Gtk::WIN_POS_CENTER);
    // Changed WINDOW_TYPE_HINT_KEYBOARD to WINDOW_TYPE_HINT_TOOLBAR for compatibility
    window.set_type_hint(Gdk::WINDOW_TYPE_HINT_TOOLBAR); // Hint to window manager that it's a toolbar/utility window

    // Create a vertical box to hold the keyboard and debug label
    Gtk::VBox layout;
    window.add(layout);

    std::cout << "DEBUG: Creating new Keyboard instance..." << std::endl;
    Keyboard* keyboard = Gtk::manage(new Keyboard());
    std::cout << "DEBUG: Keyboard instance created." << std::endl;

    layout.pack_start(*keyboard, Gtk::PACK_EXPAND_WIDGET, 0); // Pack keyboard to expand

    // Create and pack the debug label
    Gtk::Label debug_label("Ready.");
    debug_label.set_halign(Gtk::ALIGN_START); // Align text to the start (left)
    debug_label.set_margin_left(10);
    debug_label.set_margin_right(10);
    debug_label.set_margin_bottom(5);
    layout.pack_end(debug_label, Gtk::PACK_SHRINK, 0); // Pack at the end, don't expand
    g_debug_label = &debug_label; // Assign to global pointer

    std::cout << "DEBUG: Packing keyboard into layout." << std::endl;
    std::cout << "DEBUG: Keyboard packed." << std::endl;

    // Connect signals for hide/show and quit
    keyboard->signal_hide_show().connect([&window]() {
        if (window.is_visible()) {
            window.hide();
            std::cout << "DEBUG: Window minimized." << std::endl;
        } else {
            window.show_all(); // If hidden, show it
            std::cout << "DEBUG: Window restored." << std::endl;
        }
    });

    keyboard->signal_quit_app().connect([&app]() {
        std::cout << "DEBUG: Quitting application." << std::endl;
        app->quit();
    });

    // CRITICAL FIX: Show all widgets in the window before running the main loop
    window.show_all(); 

    std::cout << "DEBUG: Calling app->run()..." << std::endl;
    int result = app->run(window);
    std::cout << "DEBUG: app->run() finished. Result: " << result << std::endl;

    return result;
}
