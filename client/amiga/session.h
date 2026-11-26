#ifndef BOTCHED_AMIGA_SESSION_H
#define BOTCHED_AMIGA_SESSION_H
#include "botched/client/nio.h"
#include "play.h"
enum { BA_LOAD = 1, BA_SNAPSHOT, BA_STOP, BA_EVENTS, BA_FETCH, BA_SUBMIT };
enum { BA_CANCELLED = 207 };
typedef struct {
  unsigned kind, error, size, apply, accepted, code;
  uint32_t high;
  uint8_t page[240];
} ba_notice;
typedef struct {
  uint32_t (*now)(void *);
  void (*pause)(void *);
  int (*cancelled)(void *);
  void *ctx;
} ba_hooks;
typedef struct {
  bn_client client;
  ba_world world;
  ba_notice notice;
  const bn_ops *ops;
  void *io_context;
  ba_hooks hooks;
  const char *url, *match;
  uint32_t since;
  unsigned operation, submit_round, result, resumes, close_attempts,
      load_started;
  uint8_t submit_orders[3];
} ba_session;
#ifdef __cplusplus
extern "C" {
#endif
/* Caller initializes dependencies once; BA_LOAD is never silently repeated. */
void ba_session_run(ba_session *, unsigned, uint32_t, unsigned,
                    const uint8_t[3]);
int ba_session_close(ba_session *);
#ifdef __cplusplus
}
#endif
#endif
