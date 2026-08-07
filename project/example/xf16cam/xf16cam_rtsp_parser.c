#include <limits.h>
#include <string.h>

#include "compiler.h"
#include "xf16cam_rtsp_parser.h"

__xip_text
static int xf16cam_ascii_equal(const char *text, const char *expected, size_t length)
{
	size_t i;

	for (i = 0; i < length; ++i) {
		char a = text[i];
		char b = expected[i];

		if (a >= 'A' && a <= 'Z')
			a = (char)(a + ('a' - 'A'));
		if (b >= 'A' && b <= 'Z')
			b = (char)(b + ('a' - 'A'));
		if (a != b)
			return 0;
	}
	return 1;
}

__xip_text
static int xf16cam_content_length(const char *data, size_t header_length,
				  size_t *content_length)
{
	static const char name[] = "Content-Length";
	size_t line = 0;
	int found = 0;

	*content_length = 0;
	while (line + 2U <= header_length) {
		size_t end = line;
		size_t value;
		size_t parsed = 0;
		int digits = 0;

		while (end + 1U < header_length &&
		       !(data[end] == '\r' && data[end + 1U] == '\n'))
			++end;
		if (end + 1U >= header_length)
			return -1;
		if (end == line)
			return 0;
		if (end - line <= sizeof(name) - 1U ||
		    data[line + sizeof(name) - 1U] != ':' ||
		    !xf16cam_ascii_equal(data + line, name, sizeof(name) - 1U)) {
			line = end + 2U;
			continue;
		}
		if (found)
			return -1;
		value = line + sizeof(name);
		while (value < end && (data[value] == ' ' || data[value] == '\t'))
			++value;
		while (value < end && data[value] >= '0' && data[value] <= '9') {
			size_t digit = (size_t)(data[value++] - '0');
			if (parsed > (SIZE_MAX - digit) / 10U)
				return -1;
			parsed = parsed * 10U + digit;
			digits = 1;
		}
		while (value < end && (data[value] == ' ' || data[value] == '\t'))
			++value;
		if (!digits || value != end)
			return -1;
		*content_length = parsed;
		found = 1;
		line = end + 2U;
	}
	return 0;
}

__xip_text
void xf16cam_rtsp_parser_init(XF16CamRtspParser *parser)
{
	memset(parser, 0, sizeof(*parser));
}

__xip_text
char *xf16cam_rtsp_parser_write_ptr(XF16CamRtspParser *parser)
{
	return parser->data + parser->length;
}

__xip_text
size_t xf16cam_rtsp_parser_writable(const XF16CamRtspParser *parser)
{
	return parser->message_length == 0 && parser->length <= XF16CAM_RTSP_INPUT_CAPACITY ?
	       XF16CAM_RTSP_INPUT_CAPACITY - parser->length : 0;
}

__xip_text
int xf16cam_rtsp_parser_commit(XF16CamRtspParser *parser, size_t length)
{
	if (length > xf16cam_rtsp_parser_writable(parser))
		return -1;
	parser->length += length;
	return 0;
}

__xip_text
int xf16cam_rtsp_parser_next(XF16CamRtspParser *parser, const char **request)
{
	size_t header_length = 0;
	size_t content_length;
	size_t total;
	size_t i;

	*request = NULL;
	if (parser->message_length != 0)
		return -1;
	for (;;) {
		if (parser->length != 0 && parser->data[0] == '$') {
			if (parser->length < 4U)
				return 0;
			total = 4U + ((size_t)(uint8_t)parser->data[2] << 8) +
			        (uint8_t)parser->data[3];
			if (total > XF16CAM_RTSP_INPUT_CAPACITY)
				return -1;
			if (parser->length < total)
				return 0;
			memmove(parser->data, parser->data + total, parser->length - total);
			parser->length -= total;
			continue;
		}
		for (i = 0; i + 3U < parser->length; ++i) {
			if (parser->data[i] == '\r' && parser->data[i + 1U] == '\n' &&
			    parser->data[i + 2U] == '\r' && parser->data[i + 3U] == '\n') {
				header_length = i + 4U;
				break;
			}
		}
		if (header_length == 0)
			return 0;
		if (xf16cam_content_length(parser->data, header_length, &content_length) != 0 ||
		    content_length > XF16CAM_RTSP_INPUT_CAPACITY - header_length)
			return -1;
		total = header_length + content_length;
		if (parser->length < total)
			return 0;
		parser->saved_byte = parser->data[total];
		parser->data[total] = '\0';
		parser->message_length = total;
		*request = parser->data;
		return 1;
	}
}

__xip_text
void xf16cam_rtsp_parser_consume(XF16CamRtspParser *parser)
{
	if (parser->message_length == 0)
		return;
	parser->data[parser->message_length] = parser->saved_byte;
	memmove(parser->data, parser->data + parser->message_length,
	        parser->length - parser->message_length);
	parser->length -= parser->message_length;
	parser->message_length = 0;
}
