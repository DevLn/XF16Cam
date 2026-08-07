#ifndef XF16CAM_RTSP_PARSER_H
#define XF16CAM_RTSP_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define XF16CAM_RTSP_INPUT_CAPACITY (1024U)

typedef struct {
	char data[XF16CAM_RTSP_INPUT_CAPACITY + 1U];
	size_t length;
	size_t message_length;
	char saved_byte;
} XF16CamRtspParser;

void xf16cam_rtsp_parser_init(XF16CamRtspParser *parser);
char *xf16cam_rtsp_parser_write_ptr(XF16CamRtspParser *parser);
size_t xf16cam_rtsp_parser_writable(const XF16CamRtspParser *parser);
int xf16cam_rtsp_parser_commit(XF16CamRtspParser *parser, size_t length);
int xf16cam_rtsp_parser_next(XF16CamRtspParser *parser, const char **request);
void xf16cam_rtsp_parser_consume(XF16CamRtspParser *parser);

#endif
