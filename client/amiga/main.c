#include "platform.h"
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
static ba_world shown, incoming;
static ba_editor editor = {{8, 8, 8}, 0, 0, 1, 0};
static ba_replay replay;
static ba_notice notice;
static void memory(const char *stage) {
  printf("MEM %s chip=%lu other=%lu total=%lu largest_chip=%lu "
         "largest_other=%lu\n",
         stage, AvailMem(MEMF_CHIP), AvailMem(MEMF_FAST), AvailMem(MEMF_ANY),
         AvailMem(MEMF_CHIP | MEMF_LARGEST),
         AvailMem(MEMF_FAST | MEMF_LARGEST));
  fflush(stdout);
}
int main(int argc, char **argv) {
  int quit = 0, capture = argc == 4, busy = 1, halted = 0, queued = 0, code = 0,
      redraw = 1, rebase = 0;
  unsigned actions, job = 0, delay = 0, pace = 0, send_round = 0, events = 0,
                    frames = 0;
  uint8_t orders[3];
  unsigned long worker_stack = 0, reserved;
  char status[40] = "Connecting through NIO...";
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
  if (!ba_display_open())
    return 20;
  memory("display");
  ba_draw(&shown, &editor, status);
  if (!ba_worker_start(argv[1], argv[2])) {
    puts("Cannot start NIO worker / bad arguments");
    ba_display_close();
    return 20;
  }
  while (!quit) {
    actions = ba_input(&editor);
    if (actions & BA_QUIT)
      quit = 1;
    if (actions & BA_CHANGED)
      redraw = 1;
    if ((actions & BA_SEND) && !queued) {
      queued = 1;
      send_round = editor.round;
      memcpy(orders, editor.orders, 3);
      editor.pending = 1;
      strcpy(status, "Sending programme...");
      redraw = 1;
    }
    if (actions & BA_REFRESH)
      delay = 0;
    if (ba_worker_poll(&incoming, &notice, &worker_stack)) {
      busy = 0;
      redraw = 1;
      if (notice.error) {
        editor.pending = 0;
        if (notice.error == 11) {
          if (notice.kind == BA_LOAD)
            shown = incoming;
          strcpy(status, "Waiting for match to start");
          job = BA_SNAPSHOT;
          delay = 50;
        } else if (notice.error == 15) {
          rebase = 1;
          queued = 0;
          strcpy(status, "History expired: refreshing");
          job = BA_SNAPSHOT;
        } else {
          if (notice.error == 107)
            strcpy(status, "Session expired: outcome uncertain");
          else
            sprintf(status, "Network/game error %ld", (long)notice.error);
          if (capture) {
            printf("FAULT code=%u kind=%u\n", notice.error, notice.kind);
            fflush(stdout);
          }
          halted = 1;
          code = 10;
          editor.locked = 1;
        }
      } else if (notice.kind == BA_LOAD || notice.kind == BA_SNAPSHOT) {
        if (!rebase && !ba_snapshot_ready(&replay, shown.valid, notice.high)) {
          /* A deadline may advance the server between empty EVENTS and
           * SNAPSHOT. Keep displaying the old mirror; replay the gap before
           * publication. */
          job = BA_EVENTS;
          delay = 0;
          continue;
        }
        rebase = 0;
        shown = incoming;
        ba_replay_snapshot(&replay, notice.high);
        ba_editor_sync(&editor, &shown);
        if (shown.snapshot[0] == 2) {
          strcpy(status, "MATCH FINISHED");
          job = 0;
        } else {
          strcpy(status, editor.locked ? "Accepted: waiting for players"
                                       : "Choose three orders; ENTER to ready");
          job = BA_EVENTS;
          delay = 40;
        }
        if (capture) {
          memory("snapshot");
          printf("RESULT error=0 map=%u snapshot=%u width=%u height=%u "
                 "player=%u round=%u phase=%u events=%u ack=%lu cursor=%lu "
                 "worker_stack=%lu main_stack=%lu\n",
                 shown.map_valid, shown.valid, shown.width, shown.height,
                 shown.player, shown.snapshot[1], shown.snapshot[0], events,
                 (unsigned long)replay.ack, (unsigned long)replay.cursor,
                 worker_stack, ba_stack_used());
          fflush(stdout);
          ba_capture(&shown);
        }
      } else if (notice.kind == BA_SUBMIT) {
        editor.pending = 0;
        if (notice.accepted) {
          editor.locked = 1;
          strcpy(status, "Accepted: waiting for players");
        } else {
          sprintf(status, "Submission rejected: code %ld", (long)notice.code);
          editor.locked = 1;
        }
        if (capture) {
          printf("SUBMIT round=%u orders=%u,%u,%u accepted=%u code=%u\n",
                 send_round, orders[0], orders[1], orders[2], notice.accepted,
                 notice.code);
          fflush(stdout);
        }
        job = BA_EVENTS;
        delay = 0;
      } else {
        if (!ba_replay_page(&replay, notice.page, notice.size, notice.apply)) {
          halted = 1;
          code = 10;
          strcpy(status, "Invalid replay page");
        }
        pace = 0;
        if (!replay.active)
          job = (notice.page[20] & 1) ? BA_SNAPSHOT : BA_FETCH;
      }
    }
    if (replay.active && !halted && !busy) {
      if (pace)
        --pace;
      else {
        int event = ba_replay_next(&replay, &shown);
        if (event < 0) {
          halted = 1;
          code = 10;
          strcpy(status, "Invalid event state");
        } else if (event) {
          editor.locked = 1;
          ++events;
          if (replay.actor)
            sprintf(status, "P%ld %s: %s", (long)replay.actor,
                    ba_order_name(replay.order),
                    event == 5   ? "executed"
                    : event == 6 ? "no effect"
                                 : "disabled");
          else
            strcpy(status, ba_event_name(event));
          pace = 4;
          if (capture) {
            printf("EVENT ordinal=%u type=%d round=%u tick=%u\n", events, event,
                   shown.snapshot[1], shown.snapshot[2]);
            fflush(stdout);
          }
        } else
          job = replay.final ? (replay.batch_events ? BA_EVENTS : BA_SNAPSHOT)
                             : BA_FETCH;
        redraw = 1;
      }
    }
    if (!busy && !replay.active && !halted && !quit) {
      if (queued && job != BA_FETCH && job != BA_SNAPSHOT) {
        queued = 0;
        if (send_round != shown.snapshot[1] || shown.snapshot[0] != 0) {
          editor.pending = 0;
          strcpy(status, "Round changed; programme not sent");
          redraw = 1;
        } else {
          busy = ba_worker_job(BA_SUBMIT, 0, send_round, orders);
          job = 0;
        }
      } else if (delay)
        --delay;
      else if (job) {
        busy = ba_worker_job(job, replay.cursor, 0, 0);
        job = 0;
      }
      if (!busy && editor.pending && !queued) {
        editor.pending = 0;
        halted = 1;
        code = 10;
        strcpy(status, "Cannot submit: session stopped");
        redraw = 1;
      }
    }
    if (halted)
      editor.locked = 1;
    if (++frames == 50) {
      frames = 0;
      redraw = 1;
    }
    if (redraw) {
      ba_draw(&shown, &editor, status);
      redraw = 0;
    }
    WaitTOF();
  }
  ba_draw(&shown, &editor, "Closing connection; please wait...");
  {
    unsigned close_failed = 0, waiting = 0;
    ba_worker_stop_begin();
    while (!ba_worker_stop_poll(&worker_stack, &close_failed)) {
      ba_input(&editor);
      if (++waiting == 500)
        ba_draw(&shown, &editor, "Waiting for broker; exit not safe yet");
      WaitTOF();
    }
    printf("CLOSE confirmed=%u wait_frames=%u\n", !close_failed, waiting);
    if (close_failed) {
      puts("Network Close unconfirmed; remote handle may remain until expiry");
      code = 10;
    }
  }
  ba_display_close();
  memory("closed");
  printf("EXIT main_stack=%lu worker_stack=%lu reserved_main=%lu "
         "reserved_worker=8192 events=%u\n",
         ba_stack_used(), worker_stack, reserved, events);
  return code;
}
