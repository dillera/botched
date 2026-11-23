#include "platform.h"
#include <proto/exec.h>
/* Raw-key input is independent of network jobs and world mutation. */
unsigned ba_input(void) {
  struct IntuiMessage *msg;
  unsigned actions = 0;
  while ((msg = (struct IntuiMessage *)GetMsg(ba_window->UserPort))) {
    UWORD key = msg->Code;
    ReplyMsg(&msg->ExecMessage);
    if (key == 0x45)
      actions |= BA_QUIT;
    if (key == 0x13)
      actions |= BA_REFRESH;
  }
  return actions;
}
