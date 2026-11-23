#include "platform.h"
#include <proto/exec.h>
ba_view ba_viewport;
unsigned ba_input(ba_editor *editor, const ba_world *world) {
  struct IntuiMessage *msg;
  unsigned actions = 0;
  while ((msg = (struct IntuiMessage *)GetMsg(ba_window->UserPort))) {
    UWORD key = msg->Code;
    ReplyMsg(&msg->ExecMessage);
    actions |= ba_view_key(&ba_viewport, editor, world, key);
  }
  return actions;
}
