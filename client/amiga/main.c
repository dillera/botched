#include "platform.h"
#include <dos/dosextens.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>
static ba_world shown;
static void memory(const char *stage) {
  printf("MEM %s chip=%lu other=%lu total=%lu largest_chip=%lu "
         "largest_other=%lu\n",
         stage, AvailMem(MEMF_CHIP), AvailMem(MEMF_FAST), AvailMem(MEMF_ANY),
         AvailMem(MEMF_CHIP | MEMF_LARGEST),
         AvailMem(MEMF_FAST | MEMF_LARGEST));
  fflush(stdout);
}
int main(int argc, char **argv) {
  int quit = 0, capture = argc == 4, code = 0;
  unsigned error = 0;
  unsigned long worker_stack = 0, reserved;
  char status[40];
  puts("Botched A1 starting");
  fflush(stdout);
  reserved = ba_stack_mark();
  if (argc < 3 || argc > 4 || (argc == 4 && strcmp(argv[3], "--capture"))) {
    puts("Usage: botched-amiga tcp://host:binary-port match-id [--capture]");
    return 10;
  }
  if (reserved < 16384) {
    puts("Use Shell command: Stack 16384");
    return 10;
  }
  memory("entry");
  if (!ba_display_open()) {
    puts("Cannot open OCS screen");
    return 20;
  }
  memory("display");
  ba_draw(&shown, "Connecting through NIO...");
  if (!ba_worker_start(argv[1], argv[2])) {
    puts("Cannot start NIO worker / bad arguments");
    ba_display_close();
    return 20;
  }
  while (!quit) {
    unsigned actions = ba_input();
    if (actions & BA_QUIT)
      quit = 1;
    if ((actions & BA_REFRESH) && ba_worker_refresh())
      ba_draw(&shown, "Refreshing authoritative state...");
    if (ba_worker_poll(&shown, &error, &worker_stack)) {
      if (error) {
        sprintf(status, "NIO/game error %ld; ESC to exit", (long)error);
        code = 10;
      } else
        sprintf(status, "Round %ld: %s", (long)shown.snapshot[1],
                shown.snapshot[0] == 0   ? "PLANNING"
                : shown.snapshot[0] == 1 ? "EXECUTING"
                : shown.snapshot[0] == 2 ? "FINISHED"
                                         : "WAITING");
      ba_draw(&shown, status);
      memory("snapshot");
      printf("RESULT error=%u map=%u snapshot=%u width=%u height=%u player=%u "
             "worker_stack=%lu main_stack=%lu\n",
             error, shown.map_valid, shown.valid, shown.width, shown.height,
             shown.player, worker_stack, ba_stack_used());
      fflush(stdout);
      if (capture)
        ba_capture(&shown);
    }
    WaitTOF();
  }
  ba_draw(&shown, "Closing connection; please wait...");
  worker_stack = ba_worker_stop();
  ba_display_close();
  memory("closed");
  printf("EXIT main_stack=%lu worker_stack=%lu reserved_main=%lu "
         "reserved_worker=8192\n",
         ba_stack_used(), worker_stack, reserved);
  return code;
}
