#ifndef BOTCHED_CLIENT_NIO_FUJINET_H
#define BOTCHED_CLIENT_NIO_FUJINET_H
#include "botched/client/nio.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Concrete existing fujinet-nio-lib NetworkDevice binding. Caller owns fn_init /
 * fn_shutdown and its configured platform channel; no firmware extensions. */
extern const bn_ops bn_fujinet_ops;
#ifdef __cplusplus
}
#endif
#endif
