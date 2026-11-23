#ifndef BOTCHED_THIN_STREAM_H
#define BOTCHED_THIN_STREAM_H
#include "thin.h"
#include "fn_stream.h"
#ifdef __cplusplus
extern "C" {
#endif
#ifndef __ATARI__
uint8_t TH_FAST fn_stream_init(fn_stream_io_t *);
uint8_t TH_FAST fn_stream_open(fn_stream_io_t *);
uint8_t TH_FAST fn_stream_read(fn_stream_io_t *);
uint8_t TH_FAST fn_stream_write(fn_stream_io_t *);
uint8_t TH_FAST fn_stream_close(fn_stream_io_t *);
#endif
extern fn_stream_io_t st_io;
/* Caller owns foreground scheduling and timeouts. Lower-level calls themselves
 * can block for the library's SIO timeout; see feasibility report. */
uint8_t st_open(const char *url);
uint8_t st_begin(void); /* th.tx is a prepared complete request */
int8_t st_step(void); /* 0=yield, 1=validated response ready, -1=uncertain/disconnected */
uint8_t st_close(void);
#ifdef __cplusplus
}
#endif
#endif
