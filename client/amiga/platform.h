#ifndef BOTCHED_AMIGA_PLATFORM_H
#define BOTCHED_AMIGA_PLATFORM_H
#include "session.h"
#include <exec/types.h>
#include <intuition/intuition.h>
extern struct Window *ba_window;
int ba_display_open(void);
void ba_display_close(void);
void ba_draw(const ba_world *, const ba_editor *, const char *);
enum { BA_QUIT = 1, BA_REFRESH = 2, BA_CHANGED = 4, BA_SEND = 8 };
unsigned ba_input(ba_editor *);
void ba_capture(const ba_world *);
/* Single outstanding job. Publication transfers ownership by Exec reply. */
int ba_worker_start(const char *, const char *);
int ba_worker_poll(ba_world *, ba_notice *, unsigned long *);
int ba_worker_job(unsigned kind, uint32_t cursor, unsigned round,
                  const uint8_t orders[3]);
void ba_worker_stop_begin(void);
int ba_worker_stop_poll(unsigned long *, unsigned *);
void ba_worker_entry(void);
unsigned long ba_stack_mark(void);
unsigned long ba_stack_used(void);
#endif
