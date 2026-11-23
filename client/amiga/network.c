/* The network process alone owns the NIO library, reply port and B4 client. */
#include "botched/client/nio_fujinet.h"
#include "platform.h"
#include <clib/alib_protos.h>
#include <dos/dosextens.h>
#include <fujinet-nio.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
static ba_session session;
static volatile unsigned cancel_requested;
static unsigned close_failed;
static int stopping;
static char url[226], match[17];
static struct MsgPort *commands, *replies;
static struct Message message;
static int pending, operation;
static uint32_t since;
static unsigned submit_round;
static uint8_t submit_orders[3];
static unsigned long stack_used;
extern ULONG ba_worker_seglist;
static uint32_t now(void *ctx) {
  struct DateStamp d;
  (void)ctx;
  DateStamp(&d);
  return ((uint32_t)d.ds_Days * 86400UL + d.ds_Minute * 60UL) * 1000UL +
         d.ds_Tick * 20UL;
}
static void pause_worker(void *ctx) {
  (void)ctx;
  Delay(1);
}
static int cancelled(void *ctx) {
  (void)ctx;
  return cancel_requested != 0;
}
void ba_worker_entry(void) {
  struct Process *self = (struct Process *)FindTask(0);
  struct Message *m;
  ba_stack_mark();
  WaitPort(&self->pr_MsgPort);
  m = GetMsg(&self->pr_MsgPort);
  commands = CreatePort(0, 0);
  if (!commands) {
    session.result = 205;
    stack_used = ba_stack_used();
    Forbid();
    ReplyMsg(m);
    return;
  }
  for (;;) {
    if (operation == 3) {
      close_failed = !ba_session_close(&session);
      fn_shutdown();
      DeletePort(commands);
      commands = 0;
      stack_used = ba_stack_used();
      Forbid();
      ReplyMsg(m);
      return;
    }
    if (operation == BA_LOAD && fn_init() != FN_OK) {
      session.result = 204;
      session.notice.kind = BA_LOAD;
    } else
      ba_session_run(&session, operation, since, submit_round, submit_orders);
    stack_used = ba_stack_used();
    ReplyMsg(m);
    WaitPort(commands);
    m = GetMsg(commands);
  }
}
int ba_worker_start(const char *u, const char *id) {
  if (strlen(u) > 224 || strlen(id) > 16 || !*id || strncmp(u, "tcp://", 6))
    return 0;
  cancel_requested = close_failed = stopping = 0;
  memset(&session, 0, sizeof session);
  session.url = url;
  session.match = match;
  session.ops = &bn_fujinet_ops;
  session.hooks.now = now;
  session.hooks.pause = pause_worker;
  session.hooks.cancelled = cancelled;
  strcpy(url, u);
  strcpy(match, id);
  replies = CreatePort(0, 0);
  if (!replies)
    return 0;
  commands = CreateProc((UBYTE *)"Botched network", 0,
                        (BPTR)((ULONG)&ba_worker_seglist >> 2), 8192);
  if (!commands) {
    DeletePort(replies);
    replies = 0;
    return 0;
  }
  message.mn_Node.ln_Type = NT_MESSAGE;
  message.mn_Length = sizeof message;
  message.mn_ReplyPort = replies;
  operation = BA_LOAD;
  pending = 1;
  PutMsg(commands, &message);
  return 1;
}
int ba_worker_poll(ba_world *out, ba_notice *reply, unsigned long *stack) {
  if (!pending || !GetMsg(replies))
    return 0;
  pending = 0;
  if (operation == BA_LOAD || operation == BA_SNAPSHOT)
    memcpy(out, &session.world, sizeof session.world);
  if (session.result >= 100 && session.result <= 108) {
    uint8_t stage = 0, result = 0, cause = 0, native = 0;
    uint16_t status = 0;
    /* Worker is idle until the next job; report its transport diagnostics here. */
    fn_amiga_transport_last_broker_detail(&stage, &result);
    fn_amiga_transport_last_broker_cause(&cause);
    fn_amiga_transport_last_serial_detail(&native, &status);
    printf("NIO failure app=%u broker_stage=%u broker_result=%u cause=%u "
           "serial_error=%u serial_status=%u\n",
           session.result, stage, result, cause, native, status);
    fflush(stdout);
  }
  session.notice.error = session.result;
  *reply = session.notice;
  *stack = stack_used;
  return 1;
}
int ba_worker_job(unsigned kind, uint32_t cursor, unsigned round,
                  const uint8_t orders[3]) {
  if (pending || !commands || !session.world.player || session.client.error ||
      stopping || kind < BA_SNAPSHOT || kind > BA_SUBMIT || kind == BA_STOP)
    return 0;
  operation = kind;
  since = cursor;
  submit_round = round;
  if (orders)
    memcpy(submit_orders, orders, 3);
  pending = 1;
  PutMsg(commands, &message);
  return 1;
}

void ba_worker_stop_begin(void) {
  stopping = 1;
  cancel_requested = 1;
}
int ba_worker_stop_poll(unsigned long *used, unsigned *failed) {
  if (pending) {
    if (!GetMsg(replies))
      return 0;
    pending = 0;
    if (operation == BA_STOP) {
      DeletePort(replies);
      replies = 0;
    }
  }
  if (commands) {
    operation = BA_STOP;
    pending = 1;
    PutMsg(commands, &message);
    return 0;
  }
  if (replies) {
    DeletePort(replies);
    replies = 0;
  }
  *used = stack_used;
  *failed = close_failed;
  return 1;
}
