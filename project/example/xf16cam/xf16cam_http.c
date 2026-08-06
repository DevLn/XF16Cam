#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "kernel/os/os.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"
#include "lwip/sockets.h"
#include "net/wlan/wlan.h"
#include "ota/ota.h"

#include "xf16cam_config.h"
#include "xf16cam_http.h"
#include "xf16cam_net.h"
#include "xf16cam_version.h"

#define XF16CAM_HTTP_PORT         (80)
#define XF16CAM_HTTP_REQUEST_SIZE (2048)
#define XF16CAM_HTTP_SCAN_MAX     (12)
#define XF16CAM_HTTP_STACK_SIZE   (3 * 1024)
#define XF16CAM_OTA_MAX_SIZE      (468 * 1024)
#define XF16CAM_HTTP_TIMEOUT_MS   (15000)

enum {
	XF16CAM_HTTP_KEEP_RUNNING = 0,
	XF16CAM_HTTP_COLD_REBOOT,
	XF16CAM_HTTP_OTA_REBOOT,
};

static OS_Thread_t g_http_thread;
static char g_request[XF16CAM_HTTP_REQUEST_SIZE];
static wlan_sta_ap_t g_scan_results[XF16CAM_HTTP_SCAN_MAX];

extern void heap_get_space(uint8_t **start, uint8_t **end, uint8_t **current);

static size_t xf16cam_http_heap_headroom(void)
{
	uint8_t *start;
	uint8_t *end;
	uint8_t *current;

	heap_get_space(&start, &end, &current);
	return (size_t)(end - current);
}

static const char g_page_head[] =
	"<!doctype html><html><head><meta charset=utf-8>"
	"<meta name=viewport content='width=device-width,initial-scale=1'>"
	"<title>XF16Cam</title><style>"
	"body{font:15px system-ui;margin:auto;max-width:680px;padding:18px;background:#eef2f5;color:#18212b}"
	"section{background:white;padding:16px;margin:12px 0;border-radius:10px;box-shadow:0 1px 4px #0002}"
	"h1,h2{margin:.2em 0}.grid{display:grid;grid-template-columns:max-content 1fr;gap:6px 14px}"
	"input,button{font:inherit;padding:9px;margin:5px 0;box-sizing:border-box}input{width:100%}"
	"button{cursor:pointer}small{color:#586675}.ok{color:#176b34}</style></head><body>";

static int xf16cam_http_send_all(int fd, const void *data, size_t length)
{
	const uint8_t *p = data;

	while (length > 0) {
		int sent = send(fd, p, length, 0);
		if (sent <= 0)
			return -1;
		p += sent;
		length -= sent;
	}
	return 0;
}

static void xf16cam_http_send_text(int fd, const char *text)
{
	xf16cam_http_send_all(fd, text, strlen(text));
}

static void xf16cam_http_begin(int fd, const char *status, const char *type)
{
	char header[192];
	int length = snprintf(header, sizeof(header),
	                      "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
	                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
	                      status, type);
	xf16cam_http_send_all(fd, header, length);
}

static void xf16cam_http_send_escaped(int fd, const uint8_t *text, size_t length,
	                                  int json)
{
	size_t i;
	char encoded[8];

	for (i = 0; i < length; ++i) {
		const char *replacement = NULL;
		switch (text[i]) {
		case '&': replacement = json ? "&" : "&amp;"; break;
		case '<': replacement = json ? "<" : "&lt;"; break;
		case '>': replacement = json ? ">" : "&gt;"; break;
		case '"': replacement = json ? "\\\"" : "&quot;"; break;
		case '\\': replacement = json ? "\\\\" : "\\"; break;
		default: break;
		}
		if (replacement != NULL) {
			xf16cam_http_send_text(fd, replacement);
		} else if (text[i] >= 0x20 && text[i] < 0x7f) {
			xf16cam_http_send_all(fd, &text[i], 1);
		} else if (json) {
			snprintf(encoded, sizeof(encoded), "\\u%04x", text[i]);
			xf16cam_http_send_text(fd, encoded);
		}
	}
}

static void xf16cam_http_page(int fd)
{
	const XF16CamConfig *config = xf16cam_config_get();
	char dynamic[512];
	int length;

	xf16cam_http_begin(fd, "200 OK", "text/html; charset=utf-8");
	xf16cam_http_send_text(fd, g_page_head);
	length = snprintf(dynamic, sizeof(dynamic),
	                  "<h1>XF16Cam</h1><p class=ok>Firmware %s</p>"
	                  "<section><h2>Device</h2><div class=grid>"
	                  "<b>Network mode</b><span>%s</span><b>IP address</b><span>%s</span>"
	                  "<b>Free SRAM</b><span>%lu bytes</span><b>Camera</b><span>GC0328, 320x240 JPEG</span>"
	                  "<b>Flash</b><span>1 MiB SPI NOR</span></div></section>"
	                  "<section><h2>Wi-Fi setup</h2><p>Choose a nearby network or type its SSID.</p>"
	                  "<form method=post action=/api/wifi><label>SSID"
	                  "<input name=ssid list=networks maxlength=32 required value=\"",
	                  XF16CAM_VERSION,
	                  xf16cam_net_mode() == XF16CAM_WIFI_STA ? "Station" : "Setup AP",
	                  xf16cam_net_ip(), (unsigned long)xf16cam_http_heap_headroom());
	xf16cam_http_send_all(fd, dynamic, length);
	xf16cam_http_send_escaped(fd, (const uint8_t *)config->ssid, strlen(config->ssid), 0);
	xf16cam_http_send_text(fd,
	                  "\"></label><datalist id=networks></datalist>"
	                  "<label>Password<input type=password name=password maxlength=63 autocomplete=new-password></label>"
	                  "<button type=submit>Save and reboot</button></form>"
	                  "<button type=button onclick=scan()>Refresh nearby networks</button> <span id=scan></span>"
	                  "<form method=post action=/api/ap><button type=submit>Return to setup AP</button></form></section>");
	length = snprintf(dynamic, sizeof(dynamic),
	                  "<section><h2>Camera stream</h2><p>RTSP is currently enabled at "
	                  "<a href='rtsp://%s:8554/stream'>rtsp://%s:8554/stream</a>.</p>"
	                  "<small>Browser video and the exclusive Web/RTSP selector arrive in the media milestone.</small></section>",
	                  xf16cam_net_ip(), xf16cam_net_ip());
	xf16cam_http_send_all(fd, dynamic, length);
	xf16cam_http_send_text(fd,
	                  "<script>async function scan(){let s=document.querySelector('#scan');s.textContent='Scanning...';"
	                  "try{let a=await(await fetch('/api/scan')).json(),d=document.querySelector('#networks');d.innerHTML='';"
	                  "a.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm'+(n.secure?' secured':' open');d.append(o)});"
	                  "s.textContent=a.length+' found'}catch(e){s.textContent='Scan failed'}}scan()</script>"
	                  "<section><h2>Firmware update</h2><p>Select an XF16Cam OTA image. Keep power connected until it restarts.</p>"
	                  "<input id=ota type=file accept=.img><button type=button onclick=update()>Install update</button> <span id=up></span></section>"
	                  "<script>async function update(){let f=document.querySelector('#ota').files[0],s=document.querySelector('#up');"
	                  "if(!f){s.textContent='Choose a file';return}if(!confirm('Install '+f.name+' and reboot?'))return;"
	                  "s.textContent='Uploading...';try{let r=await fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});"
	                  "s.textContent=await r.text()}catch(e){s.textContent='Connection closed; check whether the camera restarted'}}</script></body></html>");
}

static int xf16cam_http_scan(void)
{
	wlan_sta_scan_results_t results;
	int count = 0;

	if (xf16cam_net_mode() == XF16CAM_WIFI_AP) {
		wlan_ap_scan_bss_max_count(XF16CAM_HTTP_SCAN_MAX);
		if (wlan_ap_scan_once() != 0)
			return -1;
	} else {
		wlan_sta_bss_max_count(XF16CAM_HTTP_SCAN_MAX);
		if (wlan_sta_scan_once() != 0)
			return -1;
	}
	OS_MSleep(1500);
	results.ap = g_scan_results;
	results.size = XF16CAM_HTTP_SCAN_MAX;
	if (xf16cam_net_mode() == XF16CAM_WIFI_AP) {
		if (wlan_ap_scan_result(&results) != 0)
			return -1;
	} else if (wlan_sta_scan_result(&results) != 0) {
		return -1;
	}
	count = results.num;
	return count > XF16CAM_HTTP_SCAN_MAX ? XF16CAM_HTTP_SCAN_MAX : count;
}

static void xf16cam_http_scan_json(int fd)
{
	int count = xf16cam_http_scan();
	int i;
	char item[80];

	if (count < 0) {
		xf16cam_http_begin(fd, "503 Service Unavailable", "application/json");
		xf16cam_http_send_text(fd, "{\"error\":\"scan failed\"}");
		return;
	}
	xf16cam_http_begin(fd, "200 OK", "application/json");
	xf16cam_http_send_text(fd, "[");
	for (i = 0; i < count; ++i) {
		wlan_sta_ap_t *ap = &g_scan_results[i];
		if (i > 0)
			xf16cam_http_send_text(fd, ",");
		xf16cam_http_send_text(fd, "{\"ssid\":\"");
		xf16cam_http_send_escaped(fd, ap->ssid.ssid, ap->ssid.ssid_len, 1);
		snprintf(item, sizeof(item), "\",\"rssi\":%d,\"secure\":%s}",
		         ap->level, (ap->wpa_key_mgmt || ap->rsn_key_mgmt) ? "true" : "false");
		xf16cam_http_send_text(fd, item);
	}
	xf16cam_http_send_text(fd, "]");
}

static int xf16cam_hex(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int xf16cam_form_value(const char *body, const char *key,
	                          char *out, size_t out_size)
{
	size_t key_len = strlen(key);
	const char *p = body;
	size_t used = 0;

	while (*p != '\0') {
		if ((p == body || p[-1] == '&') && strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
			p += key_len + 1;
			while (*p != '\0' && *p != '&') {
				char value = *p++;
				if (value == '+') {
					value = ' ';
				} else if (value == '%' && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1])) {
					value = (char)((xf16cam_hex(p[0]) << 4) | xf16cam_hex(p[1]));
					p += 2;
				}
				if (used + 1 >= out_size)
					return -1;
				out[used++] = value;
			}
			out[used] = '\0';
			return 0;
		}
		p = strchr(p, '&');
		if (p == NULL)
			break;
		++p;
	}
	return -1;
}

static void xf16cam_http_message(int fd, const char *status, const char *message)
{
	xf16cam_http_begin(fd, status, "text/html; charset=utf-8");
	xf16cam_http_send_text(fd, g_page_head);
	xf16cam_http_send_text(fd, "<section><h1>XF16Cam</h1><p>");
	xf16cam_http_send_text(fd, message);
	xf16cam_http_send_text(fd, "</p><a href='/'>Return</a></section></body></html>");
}

static char *xf16cam_http_header(char *request, char *header_end, const char *name)
{
	char *line = strstr(request, "\r\n");
	size_t length = strlen(name);

	while (line != NULL && line < header_end) {
		line += 2;
		if (line + length < header_end && strncasecmp(line, name, length) == 0 &&
		    line[length] == ':')
			return line + length + 1;
		line = strstr(line, "\r\n");
	}
	return NULL;
}

static int xf16cam_http_ota(int fd, char *body, int body_length, int content_length)
{
	int written = 0;

	if (content_length <= 0 || content_length > XF16CAM_OTA_MAX_SIZE) {
		xf16cam_http_message(fd, "413 Payload Too Large", "Invalid OTA image size.");
		return XF16CAM_HTTP_KEEP_RUNNING;
	}
	if (ota_push_init() != OTA_STATUS_OK || ota_push_start() != OTA_STATUS_OK)
		goto fail;
	if (body_length > content_length)
		body_length = content_length;
	if (body_length > 0 && ota_push_data((uint8_t *)body, body_length) != OTA_STATUS_OK)
		goto fail;
	written = body_length;
	while (written < content_length) {
		int wanted = content_length - written;
		int count;
		if (wanted > (int)sizeof(g_request))
			wanted = sizeof(g_request);
		count = recv(fd, g_request, wanted, 0);
		if (count <= 0 || ota_push_data((uint8_t *)g_request, count) != OTA_STATUS_OK)
			goto fail;
		written += count;
	}
	if (ota_push_finish() != OTA_STATUS_OK)
		goto fail;
	xf16cam_http_begin(fd, "200 OK", "text/plain; charset=utf-8");
	xf16cam_http_send_text(fd, "Update verified. Rebooting...");
	return XF16CAM_HTTP_OTA_REBOOT;

fail:
	ota_push_stop();
	xf16cam_http_message(fd, "400 Bad Request", "OTA verification failed; the current firmware is unchanged.");
	return XF16CAM_HTTP_KEEP_RUNNING;
}

static int xf16cam_http_handle(int fd)
{
	char method[8];
	char path[96];
	char *header_end;
	char *body;
	char *length_header;
	int received = 0;
	int content_length = 0;
	char ssid[XF16CAM_SSID_MAX_LEN + 1];
	char psk[XF16CAM_PSK_MAX_LEN + 1];

	memset(g_request, 0, sizeof(g_request));
	header_end = NULL;
	while (received < (int)sizeof(g_request) - 1) {
		int count = recv(fd, g_request + received, sizeof(g_request) - 1 - received, 0);
		if (count <= 0)
			return 0;
		received += count;
		g_request[received] = '\0';
		header_end = strstr(g_request, "\r\n\r\n");
		if (header_end != NULL)
			break;
	}
	if (sscanf(g_request, "%7s %95s", method, path) != 2) {
		xf16cam_http_message(fd, "400 Bad Request", "Malformed request.");
		return 0;
	}
	header_end = strstr(g_request, "\r\n\r\n");
	if (header_end == NULL) {
		xf16cam_http_message(fd, "431 Request Header Fields Too Large", "Request headers are too large.");
		return XF16CAM_HTTP_KEEP_RUNNING;
	}
	body = header_end ? header_end + 4 : g_request + received;
	length_header = xf16cam_http_header(g_request, header_end, "Content-Length");
	if (length_header != NULL)
		content_length = atoi(length_header);

	if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ota") == 0)
		return xf16cam_http_ota(fd, body, received - (body - g_request), content_length);

	if (content_length > (int)(sizeof(g_request) - 1 - (body - g_request))) {
		xf16cam_http_message(fd, "413 Payload Too Large", "Request body is too large.");
		return XF16CAM_HTTP_KEEP_RUNNING;
	}
	while (received - (body - g_request) < content_length) {
		int count = recv(fd, g_request + received, sizeof(g_request) - 1 - received, 0);
		if (count <= 0)
			return XF16CAM_HTTP_KEEP_RUNNING;
		received += count;
		g_request[received] = '\0';
	}

	if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
		xf16cam_http_page(fd);
	} else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/scan") == 0) {
		xf16cam_http_scan_json(fd);
	} else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/wifi") == 0) {
		if (xf16cam_form_value(body, "ssid", ssid, sizeof(ssid)) != 0 ||
		    xf16cam_form_value(body, "password", psk, sizeof(psk)) != 0 ||
		    xf16cam_config_save_sta(ssid, psk) != 0) {
			xf16cam_http_message(fd, "400 Bad Request",
			                     "Invalid SSID or password. WPA passwords must contain 8 to 63 characters.");
			return 0;
		}
		xf16cam_http_message(fd, "200 OK", "Settings saved. Rebooting into station mode...");
		return XF16CAM_HTTP_COLD_REBOOT;
	} else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/ap") == 0) {
		if (xf16cam_config_save_ap() != 0) {
			xf16cam_http_message(fd, "500 Internal Server Error", "Could not save setup AP mode.");
			return 0;
		}
		xf16cam_http_message(fd, "200 OK", "Setup AP restored. Rebooting...");
		return XF16CAM_HTTP_COLD_REBOOT;
	} else {
		xf16cam_http_message(fd, "404 Not Found", "Page not found.");
	}
	return 0;
}

static void xf16cam_http_task(void *arg)
{
	int server;
	struct sockaddr_in address;
	int option = 1;

	server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (server < 0)
		goto out;
	setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(XF16CAM_HTTP_PORT);
	address.sin_addr.s_addr = INADDR_ANY;
	if (bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 ||
	    listen(server, 2) != 0) {
		closesocket(server);
		goto out;
	}
	printf("xf16cam HTTP ready: http://%s/\n", xf16cam_net_ip());
	while (1) {
		int client = accept(server, NULL, NULL);
		int action;
		if (client < 0)
			continue;
		setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &((int){XF16CAM_HTTP_TIMEOUT_MS}), sizeof(int));
		action = xf16cam_http_handle(client);
		closesocket(client);
		if (action != XF16CAM_HTTP_KEEP_RUNNING) {
			OS_MSleep(750);
			if (action == XF16CAM_HTTP_OTA_REBOOT)
				ota_reboot();
			HAL_PRCM_SetCPUABootFlag(PRCM_CPUA_BOOT_FROM_COLD_RESET);
			HAL_WDG_Reboot();
		}
	}

out:
	printf("xf16cam HTTP server failed\n");
	OS_ThreadDelete(&g_http_thread);
}

int xf16cam_http_start(void)
{
	if (OS_ThreadCreate(&g_http_thread, "xf16cam-http", xf16cam_http_task,
	                    NULL, OS_THREAD_PRIO_APP, XF16CAM_HTTP_STACK_SIZE) != OS_OK) {
		printf("xf16cam HTTP thread create failed\n");
		return -1;
	}
	return 0;
}
