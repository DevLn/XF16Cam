#ifndef XF16CAM_LOG_H
#define XF16CAM_LOG_H

#include <stdint.h>

/* Console mirror for boards without a serial connection (XF16CAM_NETLOG).
 *
 * Every printf in the system, SDK included, goes through the libc stdout
 * writer. The mirror installs its own writer that still drives the UART and
 * also keeps the most recent output in a small RAM ring. The board task
 * drains new bytes to a UDP broadcast on XF16CAM_LOG_PORT, which
 * tools/xf16cam/udplog.py on a PC timestamps and saves, and GET /api/log
 * returns whatever the ring currently holds. Nothing survives a reset, so
 * every deliberate reboot calls xf16cam_log_flush() first: the board task
 * drains one datagram per 50 ms poll and would otherwise reset with the
 * reason it just printed still in the ring. */

#define XF16CAM_LOG_PORT (5514)

typedef struct {
	uint32_t bytes;       /* bytes broadcast */
	uint32_t datagrams;   /* sends that succeeded */
	uint32_t send_errors; /* sends that failed (kept for the next poll) */
	uint32_t overruns;    /* times the ring overtook the broadcast cursor */
} XF16CamLogInfo;

#ifdef XF16CAM_NETLOG
void xf16cam_log_init(void);
void xf16cam_log_poll(void);
/* Broadcast everything still in the ring; call right before a reboot. */
void xf16cam_log_flush(void);
int xf16cam_log_read(uint32_t *cursor, char *out, uint32_t size);
const XF16CamLogInfo *xf16cam_log_info(void);
#else
static inline void xf16cam_log_init(void) {}
static inline void xf16cam_log_poll(void) {}
static inline void xf16cam_log_flush(void) {}
#endif

#endif
