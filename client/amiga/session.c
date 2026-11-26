#include "session.h"
#include <string.h>
static int wait_response(ba_session *s) {
  int rc, resumed = 0;
  do {
    if (s->hooks.cancelled(s->hooks.ctx)) {
      s->result = BA_CANCELLED;
      return 0;
    }
    rc = bn_step(&s->client, s->hooks.now(s->hooks.ctx));
    /* One additional budget only. Resume the retained exchange, never rerun
     * the logical JOIN/SUBMIT or allocate a fresh H3 key. */
    if (rc < 0 && s->client.error == BN_ERROR_TIMEOUT && !resumed &&
        bn_reconnect(&s->client, s->hooks.now(s->hooks.ctx))) {
      resumed = 1;
      ++s->resumes;
      rc = BN_WAIT;
    }
    if (rc == BN_WAIT)
      s->hooks.pause(s->hooks.ctx);
  } while (rc == BN_WAIT);
  if (rc < 0) {
    s->result = 100 + s->client.error;
    return 0;
  }
  return 1;
}
static int request(ba_session *s, uint8_t op, const uint8_t *body, unsigned n) {
  uint8_t packet[96];
  size_t size;
  if (s->client.next_exchange > 65535 ||
      !bw_encode_packet(op, (uint16_t)s->client.next_exchange, body, n, packet,
                        sizeof packet, &size) ||
      !bn_request(&s->client, packet, size, s->hooks.now(s->hooks.ctx))) {
    s->result = 200;
    return 0;
  }
  return wait_response(s);
}
static void collect(void *ctx, const uint8_t *p, size_t n) {
  ba_session *s = ctx;
  if (p[0] == 5) {
    s->notice.accepted = p[18];
    s->notice.code = p[19];
  }
  ba_record(&s->world, p, n);
}
static int transfer(ba_session *s, uint8_t op, const uint8_t *body,
                    unsigned n) {
  uint8_t fetch[8];
  bw_writer writer;
  ba_begin(&s->world, op);
  if (!request(s, op, body, n))
    return 0;
  for (;;) {
    if (s->client.reply[2] != 0x82) {
      s->result = 201;
      return 0;
    }
    if (s->client.apply &&
        !bw_visit_page(s->client.reply + 2, s->client.reply_size, collect, s)) {
      s->result = 202;
      return 0;
    }
    if (s->world.failed) {
      s->result = s->world.failed;
      return 0;
    }
    if (s->client.stream.complete)
      break;
    bw_writer_init(&writer, fetch, sizeof fetch);
    bw_write_u32(&writer, s->client.stream.transfer);
    bw_write_u32(&writer, s->client.stream.next_offset);
    if (!request(s, 3, fetch, sizeof fetch))
      return 0;
  }
  if (!ba_finish(&s->world, op)) {
    s->result = 203;
    return 0;
  }
  return 1;
}
void ba_session_run(ba_session *s, unsigned operation, uint32_t since,
                    unsigned round, const uint8_t orders[3]) {
  uint8_t scope = 0, body[18];
  bw_writer writer;
  s->operation = operation;
  s->since = since;
  s->submit_round = round;
  if (orders)
    memcpy(s->submit_orders, orders, 3);
  s->result = 0;
  memset(&s->notice, 0, sizeof s->notice);
  s->notice.kind = s->operation;
  if (s->hooks.cancelled(s->hooks.ctx)) {
    s->result = BA_CANCELLED;
    return;
  }
  if (s->operation == BA_LOAD) {
    if (s->load_started) {
      s->result = 208;
      return;
    }
    s->load_started = 1;
    if (!bn_init(&s->client, s->ops, s->io_context, s->url, 240, 3,
                 s->hooks.now(s->hooks.ctx), 30000)) {
      s->result = 200;
      return;
    }
    if (!wait_response(s))
      return;
    body[0] = strlen(s->match);
    memcpy(body + 1, s->match, body[0]);
    if (!transfer(s, 0x12, body, body[0] + 1) || !transfer(s, 0x17, &scope, 1))
      return;
  }
  if (s->client.error) {
    s->result = 100 + s->client.error;
    return;
  }
  if (s->operation == BA_LOAD || s->operation == BA_SNAPSHOT) {
    if (!transfer(s, 0x15, &scope, 1))
      return;
    s->notice.high = s->client.stream.high_water;
    if (s->world.snapshot[0] == 2)
      transfer(s, 0x18, &scope, 1);
  } else if (s->operation == BA_SUBMIT) {
    if (s->world.key_exhausted ||
        !ba_submit_body(body, s->submit_round, s->world.next_key,
                        s->submit_orders)) {
      s->result = 206;
      return;
    }
    if (s->world.next_key == 0xffffffffUL)
      s->world.key_exhausted = 1;
    else
      ++s->world.next_key;
    /* B4 retains this exact payload/key/exchange through all exchange retries.
     */
    transfer(s, 0x14, body, 18);
  } else {
    bw_writer_init(&writer, body, sizeof body);
    if (s->operation == BA_EVENTS) {
      bw_write_u8(&writer, 0);
      bw_write_u32(&writer, s->since);
      bw_write_u16(&writer, 16);
      if (!request(s, 0x16, body, 7))
        return;
    } else {
      bw_write_u32(&writer, s->client.stream.transfer);
      bw_write_u32(&writer, s->client.stream.next_offset);
      if (!request(s, 3, body, 8))
        return;
    }
    if (s->client.reply[2] != 0x82) {
      s->result = 201;
      return;
    }
    if (s->client.reply[22] & 2) {
      s->world.failed = 0;
      bw_visit_page(s->client.reply + 2, s->client.reply_size, ba_record,
                    &s->world);
      s->result = s->world.failed ? s->world.failed : 202;
      return;
    }
    s->notice.size = s->client.reply_size;
    s->notice.apply = s->client.apply;
    memcpy(s->notice.page, s->client.reply + 2, s->notice.size);
    s->notice.high = s->client.stream.high_water;
  }
}

int ba_session_close(ba_session *s) {
  unsigned attempt;
  for (attempt = 0; attempt < 3; ++attempt) {
    ++s->close_attempts;
    if (bn_dispose(&s->client))
      return 1;
    if (attempt < 2)
      s->hooks.pause(s->hooks.ctx);
  }
  /* Caller must shut down the channel, and report that remote release is
   * unconfirmed. Do not clear the handle or claim a successful Close. */
  return 0;
}
