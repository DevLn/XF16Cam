#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "compiler.h"
#include "kernel/os/os.h"
#include "driver/chip/hal_flash.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_wdg.h"
#include "lwip/sockets.h"
#include "net/wlan/wlan.h"
#include "ota/ota.h"
#include "common/framework/sysinfo.h"

#include "xf16cam_config.h"
#include "xf16cam_audio.h"
#include "xf16cam_board.h"
#include "xf16cam_http.h"
#include "xf16cam_media.h"
#include "xf16cam_net.h"
#include "xf16cam_sensor.h"
#include "xf16cam_storage.h"
#include "xf16cam_version.h"

#define XF16CAM_HTTP_PORT         (80)
#define XF16CAM_HTTP_REQUEST_SIZE (2048)
#define XF16CAM_HTTP_SCAN_MAX     (12)
#define XF16CAM_HTTP_STACK_SIZE   (3 * 1024)
#define XF16CAM_OTA_MAX_SIZE      (372 * 1024)
#define XF16CAM_HTTP_TIMEOUT_MS   (15000)

enum {
	XF16CAM_HTTP_KEEP_RUNNING = 0,
	XF16CAM_HTTP_COLD_REBOOT,
	XF16CAM_HTTP_OTA_REBOOT,
	XF16CAM_HTTP_DETACH_CLIENT,
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

static void xf16cam_http_flash_info(uint32_t *jedec, uint32_t *size)
{
	struct FlashDev *device = getFlashDev(0);
	struct FlashChip *chip = device != NULL ? getFlashChip(device) : NULL;
	uint32_t capacity;

	*jedec = 0;
	*size = 0;
	if (chip == NULL || HAL_Flash_Open(0, 1000) != HAL_OK)
		return;
	device->drv->open(chip);
	chip->jedecID(chip, jedec);
	(device->drv->close)(chip);
	HAL_Flash_Close(0);

	/* The third JEDEC byte is the binary capacity exponent for SPI NOR. */
	capacity = (*jedec >> 16) & 0xff;
	if (capacity >= 16 && capacity < 32)
		*size = 1UL << capacity;
}

__xip_rodata static const char g_page_head[] =
	"<!doctype html><html><head><meta charset=utf-8>"
	"<meta name=viewport content='width=device-width,initial-scale=1'>"
	"<title>XF16Cam</title><style>"
	":root{color-scheme:light;--ink:#18212b;--muted:#647281;--line:#dbe2e8;--brand:#176b5b;--bg:#edf2f4}"
	"*{box-sizing:border-box}body{font:15px system-ui;margin:0;background:var(--bg);color:var(--ink)}"
	"header,main{width:min(1040px,calc(100% - 28px));margin:auto}header{display:flex;align-items:center;"
	"justify-content:space-between;padding:18px 0 12px}h1{font-size:1.45rem;margin:0}h2{font-size:1.05rem;margin:0 0 14px}"
	"p{line-height:1.45}.meta{color:var(--muted);font-size:.85rem}.pill{display:inline-block;padding:5px 9px;"
	"border-radius:999px;background:#dcece8;color:#125648;font-weight:650}.viewer,.card{background:#fff;border:1px solid var(--line);"
	"border-radius:12px;box-shadow:0 2px 8px #18212b12}.viewer{padding:14px;margin-bottom:14px}.viewerTop{display:flex;"
	"justify-content:space-between;align-items:center;gap:10px;margin-bottom:10px}.screen{display:grid;place-items:center;"
	"min-height:240px;background:#111820;border-radius:8px;overflow:hidden;color:#aeb9c2}.screen img{display:block;width:100%;"
	"max-width:720px;aspect-ratio:4/3;object-fit:contain}.empty{text-align:center;padding:30px}.tabs{display:flex;gap:5px;overflow:auto;padding:5px;background:#dfe6ea;"
	"border-radius:10px;margin:0 0 14px;position:sticky;top:0;z-index:2}.tabs button{flex:1;min-width:max-content;border:0;"
	"background:transparent}.tabs button.on{background:#fff;color:var(--brand);box-shadow:0 1px 4px #0002}.panel{display:none}.panel.on{display:grid;"
	"grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.card{padding:16px}.wide{grid-column:1/-1}.grid{display:grid;"
	"grid-template-columns:max-content 1fr;gap:7px 14px}.grid b{color:#43515e}form{margin:8px 0}label{display:block;margin:8px 0}"
	"input,button{font:inherit;padding:9px 11px;margin:4px 0;border:1px solid #b9c5cd;border-radius:7px}input{width:100%;background:#fff}"
	"button{cursor:pointer;background:#f7f9fa}button.primary{background:var(--brand);border-color:var(--brand);color:#fff}"
	"small{color:var(--muted)}a{color:#096b99}@media(max-width:680px){header{align-items:flex-start}.panel.on{grid-template-columns:1fr}"
	".wide{grid-column:auto}.screen{min-height:180px}.grid{grid-template-columns:1fr}.grid b{margin-top:5px}}</style></head><body>";

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

#define XF16CAM_HTTP_JOIN_(a, b) a##b
#define XF16CAM_HTTP_JOIN(a, b) XF16CAM_HTTP_JOIN_(a, b)
#define XF16CAM_HTTP_SEND_LITERAL(fd, literal) do { \
	__xip_rodata static const char XF16CAM_HTTP_JOIN(g_http_text_, __LINE__)[] = literal; \
	xf16cam_http_send_all((fd), XF16CAM_HTTP_JOIN(g_http_text_, __LINE__), \
	                      sizeof(XF16CAM_HTTP_JOIN(g_http_text_, __LINE__)) - 1); \
} while (0)

static void xf16cam_http_begin(int fd, const char *status, const char *type)
{
	char header[192];
	int length = snprintf(header, sizeof(header),
	                      "HTTP/1.1 %s\r\nContent-Type: %s\r\n"
	                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
	                      status, type);
	xf16cam_http_send_all(fd, header, length);
}

static void xf16cam_http_begin_length(int fd, const char *status, const char *type,
	                                  size_t body_length)
{
	char header[224];
	int length = snprintf(header, sizeof(header),
	                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n"
	                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
	                      status, type, (unsigned long)body_length);
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
	const XF16CamAudioInfo *audio = xf16cam_audio_info();
	const XF16CamStorageInfo *storage = xf16cam_storage_info();
	const struct sysinfo *sysinfo = sysinfo_get();
	int camera_available = xf16cam_sensor_available();
	uint32_t flash_jedec;
	uint32_t flash_size;
	char camera_detail[48];
	char camera_output[24];
	char dynamic[640];
	int length;

	xf16cam_http_flash_info(&flash_jedec, &flash_size);
	if (camera_available) {
		snprintf(camera_detail, sizeof(camera_detail), "%s &middot; %ux%u JPEG",
		         xf16cam_sensor_name(), (unsigned int)xf16cam_sensor_width(),
		         (unsigned int)xf16cam_sensor_height());
		snprintf(camera_output, sizeof(camera_output), "%ux%u",
		         (unsigned int)xf16cam_sensor_width(),
		         (unsigned int)xf16cam_sensor_height());
	} else {
		snprintf(camera_detail, sizeof(camera_detail), "No supported sensor detected");
		snprintf(camera_output, sizeof(camera_output), "Unavailable");
	}

	xf16cam_http_begin(fd, "200 OK", "text/html; charset=utf-8");
	xf16cam_http_send_text(fd, g_page_head);
	length = snprintf(dynamic, sizeof(dynamic),
	                  "<header><div><h1>XF16Cam</h1><div class=meta>Firmware %s</div></div>"
	                  "<div><span class=pill>%s</span> <span class=meta>%s</span></div></header><main>"
	                  "<section class=viewer><div class=viewerTop><div><b>Live camera</b>"
	                  "<div class=meta>%s</div></div><span class=pill>%s</span></div>"
	                  "<div class=screen>",
	                  XF16CAM_VERSION,
	                  xf16cam_net_mode() == XF16CAM_WIFI_STA ? "Station" : "Setup AP",
	                  xf16cam_net_ip(), camera_detail,
	                  !camera_available ? "Offline" :
	                  config->media_mode == XF16CAM_MEDIA_WEB ? "Browser MJPEG" : "RTSP");
	xf16cam_http_send_all(fd, dynamic, length);
	if (config->media_mode == XF16CAM_MEDIA_WEB) {
		if (camera_available)
			XF16CAM_HTTP_SEND_LITERAL(fd,
			                  "<img id=video src=/stream.mjpeg alt='Live camera'></div>");
		else
			XF16CAM_HTTP_SEND_LITERAL(fd,
			                  "<div class=empty><b>Camera unavailable</b><p>Connect a supported sensor, then reboot.</p></div></div>");
		XF16CAM_HTTP_SEND_LITERAL(fd,
		                  "<div><button type=button id=listen onclick=toggleAudio()>Listen</button> "
		                  "<span id=audioState>Audio stopped</span></div>"
		                  "<script>let ac,reader,next=0;function ulaw(v){let u=(~v)&255,t=((u&15)<<3)+132;"
		                  "t<<=(u&112)>>4;return((u&128)?132-t:t-132)/32768}async function toggleAudio(){"
		                  "let b=document.querySelector('#listen'),s=document.querySelector('#audioState');"
		                  "if(ac){if(reader)await reader.cancel();await ac.close();ac=reader=null;b.textContent='Listen';s.textContent='Audio stopped';return}"
		                  "try{ac=new AudioContext();await ac.resume();reader=(await fetch('/stream.pcmu')).body.getReader();"
		                  "b.textContent='Stop audio';s.textContent='Listening';next=ac.currentTime+.15;while(ac){let r=await reader.read();"
		                  "if(r.done)break;let q=ac.createBuffer(1,r.value.length,8000),d=q.getChannelData(0);"
		                  "for(let i=0;i<d.length;i++)d[i]=ulaw(r.value[i]);let n=ac.createBufferSource();n.buffer=q;n.connect(ac.destination);"
		                  "let at=Math.max(next,ac.currentTime+.04);n.start(at);next=at+q.duration}}catch(e){s.textContent='Audio connection failed';"
		                  "if(ac)await ac.close();ac=reader=null;b.textContent='Listen'}}</script>");
	} else if (camera_available) {
		length = snprintf(dynamic, sizeof(dynamic),
		                  "<div><a href='rtsp://%s:8554/stream'>rtsp://%s:8554/stream</a></div></div>",
		                  xf16cam_net_ip(), xf16cam_net_ip());
		xf16cam_http_send_all(fd, dynamic, length);
	} else {
		XF16CAM_HTTP_SEND_LITERAL(fd,
		                  "<div class=empty><b>Camera unavailable</b><p>RTSP is disabled until a supported sensor is connected.</p></div></div>");
	}
	XF16CAM_HTTP_SEND_LITERAL(fd,
	                  "<form method=post action=/api/media>"
	                  "<button name=mode value=web onclick=\"let v=document.querySelector('#video');if(v)v.src=''\">Browser video</button> "
	                  "<button name=mode value=rtsp onclick=\"let v=document.querySelector('#video');if(v)v.src=''\">RTSP</button></form>"
	                  "<small>Only one mode is initialized at a time; changing it reboots the camera.</small></section>"
	                  "<nav class=tabs><button data-tab=live onclick=tab('live')>Live</button>"
	                  "<button data-tab=network onclick=tab('network')>Network</button>"
	                  "<button data-tab=storage onclick=tab('storage')>Storage</button>"
	                  "<button data-tab=system onclick=tab('system')>System</button></nav>"
	                  "<div id=live class=panel><section class=card><h2>Camera</h2><div class=grid>"
	                  "<b>Sensor</b><span>");
	xf16cam_http_send_text(fd, xf16cam_sensor_name());
	length = snprintf(dynamic, sizeof(dynamic),
	                  "</span><b>JPEG output</b><span>%s</span><b>Stream mode</b><span>%s</span></div></section>"
	                  "<section class=card><h2>Audio</h2><div class=grid><b>Microphone</b><span>%s</span>"
	                  "<b>Peak level</b><span><span id=mic>%u</span>/32768</span></div></section></div>",
	                  camera_output, !camera_available ? "Disabled (no sensor)" :
	                  config->media_mode == XF16CAM_MEDIA_WEB ? "Browser MJPEG" : "RTSP",
	                  audio->active ? "AMIC active, PCMU/8000" : "AMIC unavailable", audio->peak);
	xf16cam_http_send_all(fd, dynamic, length);

	XF16CAM_HTTP_SEND_LITERAL(fd,
	                  "<div id=network class=panel><section class='card wide'><h2>Wi-Fi setup</h2>"
	                  "<p>Choose a nearby network or type its SSID.</p>"
	                  "<form method=post action=/api/wifi><label>SSID"
	                  "<input name=ssid list=networks maxlength=32 required value=\"");
	xf16cam_http_send_escaped(fd, (const uint8_t *)config->ssid, strlen(config->ssid), 0);
	XF16CAM_HTTP_SEND_LITERAL(fd,
	                  "\"></label><datalist id=networks></datalist>"
	                  "<label>Password<input type=password name=password maxlength=63 autocomplete=new-password></label>"
	                  "<button class=primary type=submit>Save and reboot</button></form>"
	                  "<button type=button onclick=scan()>Scan nearby networks</button> <span id=scan></span>"
	                  "<form method=post action=/api/ap><button type=submit>Return to setup AP</button></form></section></div>"
	                  "<div id=storage class=panel><section class='card wide'><h2>SD card</h2><div class=grid>"
	                  "<b>Status</b><span>");
	if (storage->mounted) {
		length = snprintf(dynamic, sizeof(dynamic),
		                  "Mounted</span><b>Capacity</b><span>%lu MiB</span>"
		                  "<b>Free space</b><span>%lu MiB</span></div>",
		                  (unsigned long)storage->total_mb, (unsigned long)storage->free_mb);
		xf16cam_http_send_all(fd, dynamic, length);
	} else {
		XF16CAM_HTTP_SEND_LITERAL(fd, "Not mounted</span></div>");
	}
	XF16CAM_HTTP_SEND_LITERAL(fd,
	                  "<form method=post action=/api/sd/refresh><button type=submit>Check card</button></form>"
	                  "<form method=post action=/api/sd/format onsubmit=\"return confirm('Erase and format the SD card?')\">"
	                  "<button type=submit>Format FAT32</button></form>"
	                  "<small>Formatting permanently erases the card.</small></section></div>"
	                  "<div id=system class=panel><section class=card><h2>Device</h2><div class=grid>");
	length = snprintf(dynamic, sizeof(dynamic),
	                  "<b>Network mode</b><span>%s</span><b>IP address</b><span>%s</span>"
	                  "<b>Wi-Fi MAC</b><span>%02X:%02X:%02X:%02X:%02X:%02X (eFuse)</span>"
	                  "<b>Free SRAM</b><span>%lu bytes</span><b>Flash JEDEC ID</b><span>%02lX %02lX %02lX</span>"
	                  "<b>Flash capacity</b><span>%lu KiB</span><b>Mode button</b><span>PA15 (%s)</span>"
	                  "<b>Setup button</b><span>PA20 (%s)</span></div></section>",
	                  xf16cam_net_mode() == XF16CAM_WIFI_STA ? "Station" : "Setup AP", xf16cam_net_ip(),
	                  sysinfo->mac_addr[0], sysinfo->mac_addr[1], sysinfo->mac_addr[2],
	                  sysinfo->mac_addr[3], sysinfo->mac_addr[4], sysinfo->mac_addr[5],
	                  (unsigned long)xf16cam_http_heap_headroom(),
	                  (unsigned long)(flash_jedec & 0xff),
	                  (unsigned long)((flash_jedec >> 8) & 0xff),
	                  (unsigned long)((flash_jedec >> 16) & 0xff),
	                  (unsigned long)(flash_size / 1024),
	                  xf16cam_board_mode_button_pressed() ? "pressed" : "released",
	                  xf16cam_board_reset_button_pressed() ? "pressed" : "released");
	xf16cam_http_send_all(fd, dynamic, length);
	XF16CAM_HTTP_SEND_LITERAL(fd,
	                  "<section class=card><h2>XF16 pin map</h2><div class=grid>"
	                  "<b>Camera CSI</b><span>PA0-PA11</span>"
	                  "<b>Camera control</b><span>PA14</span>"
	                  "<b>Status LED</b><span>PA21 (factory-confirmed)</span>"
	                  "<b>Camera power rail</b><span>PA23</span>"
	                  "<b>SD card</b><span>PB16 CMD, PB17 D0, PB18 CLK</span>"
	                  "<b>Console</b><span>PB0 TX, PB1 RX</span>"
	                  "<b>SPI flash</b><span>PB2-PB7</span>"
	                  "<b>Mode button</b><span>PA15; short press switches Web/RTSP</span>"
	                  "<b>Setup button</b><span>PA20; hold 3 seconds to restore AP</span>"
	                  "</div></section>");
	XF16CAM_HTTP_SEND_LITERAL(fd,
	                  "<section class='card wide'><h2>Firmware update</h2><p>Select an XF16Cam OTA image. Keep power connected until it restarts.</p>"
	                  "<input id=ota type=file accept=.img><button class=primary type=button onclick=update()>Install update</button> <span id=up></span></section></div>"
	                  "<script>function tab(id){document.querySelectorAll('.panel').forEach(e=>e.classList.toggle('on',e.id==id));"
	                  "document.querySelectorAll('.tabs button').forEach(e=>e.classList.toggle('on',e.dataset.tab==id))}"
	                  "async function scan(){let s=document.querySelector('#scan');s.textContent='Scanning...';"
	                  "try{let a=await(await fetch('/api/scan')).json(),d=document.querySelector('#networks');d.innerHTML='';"
	                  "a.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.label=n.rssi+' dBm'+(n.secure?' secured':' open');d.append(o)});"
	                  "s.textContent=a.length+' found'}catch(e){s.textContent='Scan failed'}}"
	                  "async function update(){let f=document.querySelector('#ota').files[0],s=document.querySelector('#up');"
	                  "if(!f){s.textContent='Choose a file';return}if(!confirm('Install '+f.name+' and reboot?'))return;"
	                  "s.textContent='Uploading...';try{let r=await fetch('/api/ota',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});"
	                  "s.textContent=await r.text()}catch(e){s.textContent='Connection closed; check whether the camera restarted'}}"
	                  "setInterval(async()=>{try{let a=await(await fetch('/api/audio')).json(),m=document.querySelector('#mic');"
	                  "if(m)m.textContent=a.peak}catch(e){}},1000);tab('live')</script></main></body></html>");
}

static void xf16cam_http_audio_json(int fd)
{
	const XF16CamAudioInfo *audio = xf16cam_audio_info();
	char body[128];
	int length = snprintf(body, sizeof(body),
	                      "{\"active\":%s,\"peak\":%u,\"mean\":%u,\"packets\":%lu,\"read_errors\":%lu}",
	                      audio->active ? "true" : "false", audio->peak, audio->mean,
	                      (unsigned long)audio->packets, (unsigned long)audio->read_errors);

	xf16cam_http_begin_length(fd, "200 OK", "application/json", length);
	xf16cam_http_send_all(fd, body, length);
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
	static const char prefix[] = "<section><h1>XF16Cam</h1><p>";
	static const char suffix[] = "</p><a href='/'>Return</a></section></body></html>";
	size_t length = sizeof(g_page_head) - 1 + sizeof(prefix) - 1 + strlen(message) + sizeof(suffix) - 1;

	xf16cam_http_begin_length(fd, status, "text/html; charset=utf-8", length);
	xf16cam_http_send_all(fd, g_page_head, sizeof(g_page_head) - 1);
	xf16cam_http_send_all(fd, prefix, sizeof(prefix) - 1);
	xf16cam_http_send_text(fd, message);
	xf16cam_http_send_all(fd, suffix, sizeof(suffix) - 1);
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
	{
		static const char success[] = "Update verified. Rebooting...";
		xf16cam_http_begin_length(fd, "200 OK", "text/plain; charset=utf-8", sizeof(success) - 1);
		xf16cam_http_send_all(fd, success, sizeof(success) - 1);
	}
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
	char mode[8];

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
	} else if (strcmp(method, "GET") == 0 && strcmp(path, "/stream.mjpeg") == 0) {
		if (!xf16cam_sensor_available())
			xf16cam_http_message(fd, "503 Service Unavailable", "No supported camera sensor is available.");
		else if (xf16cam_config_get()->media_mode != XF16CAM_MEDIA_WEB)
			xf16cam_http_message(fd, "409 Conflict", "Browser video mode is not active.");
		else if (xf16cam_mjpeg_start(fd) != 0)
			xf16cam_http_message(fd, "503 Service Unavailable", "A browser video client is already active.");
		else
			return XF16CAM_HTTP_DETACH_CLIENT;
	} else if (strcmp(method, "GET") == 0 && strcmp(path, "/stream.pcmu") == 0) {
		if (xf16cam_config_get()->media_mode != XF16CAM_MEDIA_WEB)
			xf16cam_http_message(fd, "409 Conflict", "Browser video mode is not active.");
		else if (xf16cam_audio_http_start(fd) != 0)
			xf16cam_http_message(fd, "503 Service Unavailable", "A browser audio client is already active.");
		else
			return XF16CAM_HTTP_DETACH_CLIENT;
	} else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/scan") == 0) {
		xf16cam_http_scan_json(fd);
	} else if (strcmp(method, "GET") == 0 && strcmp(path, "/api/audio") == 0) {
		xf16cam_http_audio_json(fd);
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
	} else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/media") == 0) {
		if (xf16cam_form_value(body, "mode", mode, sizeof(mode)) != 0 ||
		    xf16cam_config_save_media(strcmp(mode, "web") == 0 ? XF16CAM_MEDIA_WEB :
		                              strcmp(mode, "rtsp") == 0 ? XF16CAM_MEDIA_RTSP : 0) != 0) {
			xf16cam_http_message(fd, "400 Bad Request", "Invalid camera mode.");
			return XF16CAM_HTTP_KEEP_RUNNING;
		}
		xf16cam_http_message(fd, "200 OK", "Camera mode saved. Rebooting...");
		return XF16CAM_HTTP_COLD_REBOOT;
	} else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/sd/refresh") == 0) {
		if (xf16cam_storage_refresh() != 0)
			xf16cam_http_message(fd, "503 Service Unavailable", "No readable FAT SD card was found.");
		else
			xf16cam_http_message(fd, "200 OK", "SD card mounted. Return to the main page for capacity details.");
	} else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/sd/format") == 0) {
		if (xf16cam_storage_format() != 0)
			xf16cam_http_message(fd, "500 Internal Server Error", "SD card formatting failed.");
		else
			xf16cam_http_message(fd, "200 OK", "SD card formatted as FAT32.");
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
		setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &((int){XF16CAM_HTTP_TIMEOUT_MS}), sizeof(int));
		action = xf16cam_http_handle(client);
		if (action == XF16CAM_HTTP_DETACH_CLIENT)
			continue;
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
