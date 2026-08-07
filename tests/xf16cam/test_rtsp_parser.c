#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xf16cam_rtsp_parser.h"

static void feed(XF16CamRtspParser *parser, const void *data, size_t length)
{
	assert(length <= xf16cam_rtsp_parser_writable(parser));
	memcpy(xf16cam_rtsp_parser_write_ptr(parser), data, length);
	assert(xf16cam_rtsp_parser_commit(parser, length) == 0);
}

static void expect_request(XF16CamRtspParser *parser, const char *expected)
{
	const char *request;

	assert(xf16cam_rtsp_parser_next(parser, &request) == 1);
	assert(strcmp(request, expected) == 0);
	xf16cam_rtsp_parser_consume(parser);
}

static void test_split_and_coalesced(void)
{
	static const char first[] = "OPTIONS rtsp://cam/stream RTSP/1.0\r\nCSeq: 1\r\n\r\n";
	static const char second[] = "PLAY rtsp://cam/stream RTSP/1.0\r\nCSeq: 2\r\n\r\n";
	XF16CamRtspParser parser;
	const char *request;

	xf16cam_rtsp_parser_init(&parser);
	feed(&parser, first, 7);
	assert(xf16cam_rtsp_parser_next(&parser, &request) == 0);
	feed(&parser, first + 7, sizeof(first) - 1U - 7U);
	feed(&parser, second, sizeof(second) - 1U);
	expect_request(&parser, first);
	expect_request(&parser, second);
	assert(xf16cam_rtsp_parser_next(&parser, &request) == 0);
}

static void test_body_and_interleaved_rtcp(void)
{
	static const unsigned char rtcp[] = { '$', 1, 0, 4, 0x81, 0xc9, 0, 0 };
	static const char request[] =
		"GET_PARAMETER rtsp://cam/stream RTSP/1.0\r\n"
		"cOnTeNt-LeNgTh: 4\r\nCSeq: 3\r\n\r\nping";
	XF16CamRtspParser parser;
	const char *parsed;

	xf16cam_rtsp_parser_init(&parser);
	feed(&parser, rtcp, sizeof(rtcp));
	feed(&parser, request, sizeof(request) - 3U);
	assert(xf16cam_rtsp_parser_next(&parser, &parsed) == 0);
	feed(&parser, request + sizeof(request) - 3U, 2U);
	expect_request(&parser, request);
}

static void test_rejects_malformed_lengths(void)
{
	static const char oversized[] =
		"OPTIONS * RTSP/1.0\r\nContent-Length: 1024\r\n\r\n";
	static const char duplicate[] =
		"OPTIONS * RTSP/1.0\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n";
	static const unsigned char oversized_rtcp[] = { '$', 1, 4, 1 };
	XF16CamRtspParser parser;
	const char *request;

	xf16cam_rtsp_parser_init(&parser);
	feed(&parser, oversized, sizeof(oversized) - 1U);
	assert(xf16cam_rtsp_parser_next(&parser, &request) == -1);
	xf16cam_rtsp_parser_init(&parser);
	feed(&parser, duplicate, sizeof(duplicate) - 1U);
	assert(xf16cam_rtsp_parser_next(&parser, &request) == -1);
	xf16cam_rtsp_parser_init(&parser);
	feed(&parser, oversized_rtcp, sizeof(oversized_rtcp));
	assert(xf16cam_rtsp_parser_next(&parser, &request) == -1);
}

int main(void)
{
	test_split_and_coalesced();
	test_body_and_interleaved_rtcp();
	test_rejects_malformed_lengths();
	puts("xf16cam RTSP parser tests passed");
	return 0;
}
