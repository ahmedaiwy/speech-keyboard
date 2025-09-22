#include "keyboardentry.h"

KeyboardEntry::KeyboardEntry(){
  set_editable(false);
  set_text("");
}

KeyboardEntry::~KeyboardEntry(){
    // Nothing specific to clean up for this simple class
}

void KeyboardEntry::append_text(Glib::ustring text){
  set_text(get_text() + text);
}
