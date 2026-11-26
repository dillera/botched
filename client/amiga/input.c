#include "platform.h"
#include <proto/exec.h>
static unsigned long dispatch_start;
unsigned ba_input_kind;
/* Same PAL/DOS clock as draw timings. This excludes time in the input queue;
 * injected Intuition message timestamps are not a reliable latency clock. */
unsigned long ba_input_latency(void) {
  return (ba_display_time() - dispatch_start) * 20;
}
ba_view ba_viewport;
unsigned ba_input(ba_editor *editor, const ba_world *world) {
  struct IntuiMessage *msg;
  unsigned actions = 0;
  while ((msg = (struct IntuiMessage *)GetMsg(ba_window->UserPort))) {
    ULONG type = msg->Class;
    UWORD key = msg->Code;
    unsigned action = 0;
    WORD x = msg->MouseX, y = msg->MouseY;
    ReplyMsg(&msg->ExecMessage);
    if (type == IDCMP_RAWKEY)
      action = ba_view_key(&ba_viewport, editor, world, key);
    else if (type == IDCMP_MOUSEBUTTONS && key == MENUDOWN &&
             (ba_viewport.panel || ba_viewport.inspect))
      action = ba_view_key(&ba_viewport, editor, world, 0x45);
    else if (type == IDCMP_MOUSEBUTTONS && key == SELECTDOWN)
      action = ba_view_click(&ba_viewport, editor, world, x, y);
    if ((action & BA_CHANGED) && !(actions & BA_CHANGED)) {
      dispatch_start = ba_display_time();
      ba_input_kind = type == IDCMP_MOUSEBUTTONS ? 2 : 1;
    }
    actions |= action;
  }
  return actions;
}
