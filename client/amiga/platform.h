#ifndef BOTCHED_AMIGA_PLATFORM_H
#define BOTCHED_AMIGA_PLATFORM_H
#include "model.h"
#include <exec/types.h>
#include <intuition/intuition.h>
extern struct Window *ba_window;
int ba_display_open(void);
void ba_display_close(void);
void ba_draw(const ba_world *, const char *);
enum { BA_QUIT = 1, BA_REFRESH = 2 };
unsigned ba_input(void);
void ba_capture(const ba_world *);
/* Single outstanding job. Publication transfers ownership by Exec reply. */
int ba_worker_start(const char *, const char *);
int ba_worker_poll(ba_world *, unsigned *, unsigned long *);
int ba_worker_refresh(void);
unsigned long ba_worker_stop(void);
void ba_worker_entry(void);
unsigned long ba_stack_mark(void);
unsigned long ba_stack_used(void);
#endif
