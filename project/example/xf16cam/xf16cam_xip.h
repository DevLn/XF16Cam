#ifndef XF16CAM_XIP_H
#define XF16CAM_XIP_H

#include <stdio.h>

#include "compiler.h"

/* appos.ld collects .rodata into the same > RAM output section as .text, so a
 * plain string literal lands in the 64 KiB SRAM-loaded app slot no matter how
 * the enclosing function is marked -- __xip_text moves only the code. Only
 * .xip_rodata reaches XIP flash, where there are tens of KiB to spare.
 *
 * snprintf format strings are the bulk of that rodata in xf16cam_http.c and
 * main.c, so XF16CAM_XIP_FORMAT parks each one in flash and calls snprintf
 * with a pointer to it. Reading a format string from XIP is safe; only
 * disabling flash while executing from it is not, which is why the two
 * flash-touching HTTP handlers stay in SRAM and CI enforces that separately.
 *
 * Two things to know before converting a call:
 *
 * - gcc still checks the format against the arguments: GCC 8 follows a static
 *   const char[] initialised from a literal, so a type mismatch warns exactly
 *   as it would on a bare snprintf. Read the build log, though -- the SDK is
 *   not built with -Werror. What nothing catches is a mangled literal, so
 *   diff the literals before and after a bulk conversion
 *   (grep -o '"[^"]*"' file | sort).
 * - A #ifdef cannot appear inside a macro invocation. Resolve a build-variant
 *   difference to a local first, the way xf16cam_http_page() does with
 *   mode_button.
 *
 * The trailing ##__VA_ARGS__ lets a constant string with no conversions use
 * the macro too.
 */
#define XF16CAM_XIP_JOIN_(a, b) a##b
#define XF16CAM_XIP_JOIN(a, b) XF16CAM_XIP_JOIN_(a, b)

#define XF16CAM_XIP_FORMAT(buf, size, literal, ...) ({ \
	__xip_rodata static const char XF16CAM_XIP_JOIN(g_xip_fmt_, __LINE__)[] = literal; \
	snprintf((buf), (size), XF16CAM_XIP_JOIN(g_xip_fmt_, __LINE__), ##__VA_ARGS__); \
})

#endif /* XF16CAM_XIP_H */
