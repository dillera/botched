#include "platform.h"
#include <dos/dosextens.h>
#include <proto/exec.h>
/* OS1.3 CLI runs commands on a DOS stack without changing tc_SPLower/Upper.
 * pr_ReturnAddr bounds the current command frame; cli_DefaultStack is in
 * longwords. Leave the bottom 1 KiB untouched to exclude DOS framing.
 * CreateProc workers use their actual Exec stack bounds. No OS2 StackSwap
 * dependency. */
static unsigned long bounds(unsigned char **lo, unsigned char **hi) {
  struct Process *p = (struct Process *)FindTask(0);
  unsigned long size;
  if (p->pr_CLI) {
    struct CommandLineInterface *cli = (void *)((ULONG)p->pr_CLI << 2);
    size = (unsigned long)cli->cli_DefaultStack * 4;
    if (size < 2048 || size > 1048576 || !p->pr_ReturnAddr)
      return 0;
    *hi = p->pr_ReturnAddr;
    *lo = *hi - size + 1024;
  } else {
    *lo = p->pr_Task.tc_SPLower;
    *hi = p->pr_Task.tc_SPUpper;
    size = *hi - *lo;
  }
  return size;
}
unsigned long ba_stack_mark(void) {
  unsigned char *lo, *hi, *sp, *p;
  unsigned long size = bounds(&lo, &hi);
  __asm volatile("move.l %%sp,%0" : "=r"(sp));
  if (!size || sp < lo + 512 || sp > hi)
    return 0;
  for (p = lo; p + 512 < sp; ++p)
    *p = 0xa5;
  return size;
}
unsigned long ba_stack_used(void) {
  unsigned char *lo, *hi, *p;
  if (!bounds(&lo, &hi))
    return 0;
  for (p = lo; p < hi && *p == 0xa5; ++p) {
  }
  return hi - p;
}
