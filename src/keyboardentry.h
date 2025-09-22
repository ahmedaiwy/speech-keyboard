#ifndef KEYBOARDENTRY_H
#define KEYBOARDENTRY_H

#include <gtkmm/entry.h>
#include <glibmm/ustring.h>

class KeyboardEntry : public Gtk::Entry{
public:
  KeyboardEntry();
  virtual ~KeyboardEntry();

  void append_text(Glib::ustring text);
};

#endif // KEYBOARDENTRY_H
