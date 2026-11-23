#include "platform.h"
#include <proto/exec.h>
/* Raw-key input is independent of network jobs and world mutation. */
unsigned ba_input(ba_editor *editor) {
  struct IntuiMessage *msg;
  unsigned actions = 0;
  while ((msg = (struct IntuiMessage *)GetMsg(ba_window->UserPort))) {
    int edit;
    UWORD key = msg->Code;
    ReplyMsg(&msg->ExecMessage);
    if (key == 0x45)
      actions |= BA_QUIT;
    if (key == 0x13)
      actions |= BA_REFRESH;
    edit = ba_edit(editor, key);
    if (edit == 1)
      actions |= BA_CHANGED;
    if (edit == 2) {
      editor->pending = 1;
      actions |= BA_SEND;
    }
  }
  return actions;
}
