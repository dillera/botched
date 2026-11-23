/* The network process alone owns the NIO library, reply port and B4 client. */
#include "botched/client/nio_fujinet.h"
#include "platform.h"
#include <clib/alib_protos.h>
#include <dos/dosextens.h>
#include <fujinet-nio.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <string.h>
static bn_client client;
static ba_world world;
static char url[226], match[17];
static struct MsgPort *commands, *replies;
static struct Message message;
static int pending, operation;
static unsigned result;
static unsigned long stack_used;
extern ULONG ba_worker_seglist;
static uint32_t now(void) {
  struct DateStamp d;
  DateStamp(&d);
  return ((uint32_t)d.ds_Days * 86400UL + d.ds_Minute * 60UL) * 1000UL +
         d.ds_Tick * 20UL;
}
static int wait_response(void) {
  int rc;
  do {
    rc = bn_step(&client, now());
    if (rc == BN_WAIT)
      Delay(1);
  } while (rc == BN_WAIT);
  if (rc < 0) {
    result = 100 + client.error;
    return 0;
  }
  return 1;
}
static int request(uint8_t op, const uint8_t *body, unsigned n) {
  uint8_t packet[96];
  size_t size;
  if (client.next_exchange > 65535 ||
      !bw_encode_packet(op, (uint16_t)client.next_exchange, body, n, packet,
                        sizeof packet, &size) ||
      !bn_request(&client, packet, size, now())) {
    result = 200;
    return 0;
  }
  return wait_response();
}
static int transfer(uint8_t op, const uint8_t *body, unsigned n) {
  uint8_t fetch[8];
  bw_writer writer;
  ba_begin(&world, op);
  if (!request(op, body, n))
    return 0;
  for (;;) {
    if (client.reply[2] != 0x82) {
      result = 201;
      return 0;
    }
    if (client.apply && !bw_visit_page(client.reply + 2, client.reply_size,
                                       ba_record, &world)) {
      result = 202;
      return 0;
    }
    if (world.failed) {
      result = world.failed;
      return 0;
    }
    if (client.stream.complete)
      break;
    bw_writer_init(&writer, fetch, sizeof fetch);
    bw_write_u32(&writer, client.stream.transfer);
    bw_write_u32(&writer, client.stream.next_offset);
    if (!request(3, fetch, sizeof fetch))
      return 0;
  }
  if (!ba_finish(&world, op)) {
    result = 203;
    return 0;
  }
  return 1;
}
static void load(void) {
  uint8_t scope = 0, join[17];
  result = 0;
  if (operation == 1) {
    if (fn_init() != FN_OK) {
      result = 204;
      return;
    }
    if (!bn_init(&client, &bn_fujinet_ops, 0, url, 240, 3, now(), 30000) ||
        !wait_response())
      return;
    join[0] = strlen(match);
    memcpy(join + 1, match, join[0]);
    if (!transfer(0x12, join, join[0] + 1))
      return;
    if (!transfer(0x17, &scope, 1))
      return;
  }
  transfer(0x15, &scope, 1);
}
void ba_worker_entry(void) {
  struct Process *self = (struct Process *)FindTask(0);
  struct Message *m;
  ba_stack_mark();
  WaitPort(&self->pr_MsgPort);
  m = GetMsg(&self->pr_MsgPort);
  commands = CreatePort(0, 0);
  if (!commands) {
    result = 205;
    stack_used = ba_stack_used();
    Forbid();
    ReplyMsg(m);
    return;
  }
  for (;;) {
    if (operation == 3) {
      bn_dispose(&client);
      fn_shutdown();
      DeletePort(commands);
      commands = 0;
      stack_used = ba_stack_used();
      Forbid();
      ReplyMsg(m);
      return;
    }
    load();
    stack_used = ba_stack_used();
    ReplyMsg(m);
    WaitPort(commands);
    m = GetMsg(commands);
  }
}
int ba_worker_start(const char *u, const char *id) {
  if (strlen(u) > 225 || strlen(id) > 16 || !*id || strncmp(u, "tcp://", 6))
    return 0;
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
  operation = 1;
  pending = 1;
  PutMsg(commands, &message);
  return 1;
}
int ba_worker_poll(ba_world *out, unsigned *error, unsigned long *stack) {
  if (!pending || !GetMsg(replies))
    return 0;
  pending = 0;
  memcpy(out, &world, sizeof world);
  *error = result;
  *stack = stack_used;
  return 1;
}
int ba_worker_refresh(void) {
  if (pending || !commands || !world.player || client.error)
    return 0;
  operation = 2;
  pending = 1;
  PutMsg(commands, &message);
  return 1;
}
unsigned long ba_worker_stop(void) {
  if (!replies)
    return 0;
  if (pending) {
    WaitPort(replies);
    GetMsg(replies);
    pending = 0;
  }
  if (commands) {
    operation = 3;
    PutMsg(commands, &message);
    WaitPort(replies);
    GetMsg(replies);
  }
  DeletePort(replies);
  replies = 0;
  return stack_used;
}
