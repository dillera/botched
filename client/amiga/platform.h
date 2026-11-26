#ifndef BOTCHED_AMIGA_PLATFORM_H
#define BOTCHED_AMIGA_PLATFORM_H
#include "session.h"
#include "view.h"
#include "visual.h"
#include "narration.h"
extern ba_view ba_viewport;
#include <exec/types.h>
#include <intuition/intuition.h>
extern struct Window *ba_window;
int ba_display_open(void);
void ba_display_close(void);
void ba_draw(const ba_world *, const ba_editor *, const char *,
             const ba_animation *);
void ba_display_metrics(void);
unsigned long ba_display_time(void);
void ba_ui_draw(struct RastPort *, struct RastPort *, const ba_world *,
                const ba_editor *, const char *);
extern unsigned ba_input_kind;
unsigned long ba_input_latency(void);
unsigned ba_input(ba_editor *, const ba_world *);
void ba_ui_reset(void);
void ba_ui_icon(struct RastPort *, unsigned, int, int);
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
