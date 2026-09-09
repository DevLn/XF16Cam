# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repository is

A fork of the **XRADIO Skylark SDK** (vendor C SDK for XR872/XR808 Cortex-M4
Wi-Fi SoCs) carrying one application: **XF16Cam**, network-camera firmware for
the 1 MiB-flash XR872ET/XF16 A9 camera board.

Everything XF16Cam-specific lives in `project/example/xf16cam/`. The rest of the
tree (`src/`, `include/`, `lib/`, `project/common/`, `chip.mk`, `gcc.mk`,
`config.mk`) is vendor SDK with its original BSD notices — **keep camera
behavior inside `project/example/xf16cam/` wherever possible** rather than
patching the SDK.

`project/example/xf16cam/readme.md` is the project guide (sensor list and
validation status, pin map, flash layout, power/battery plan). Read it before
touching sensors, GPIO, or the flash budget — but **it lags the code**: it was
last updated at `aaa71ca` and still describes single-client RTSP, with no
mention of automatic day/night switching, PTZ control, or capture-stall
recovery. Trust the source over the readme, and update the readme when you
change behavior it documents.

`FEATURES.md` at the repo root is a code-derived table of every UI, API, and
automatic behavior — useful as an index before changing the HTTP surface.

## Build

The only supported toolchain is **Arm GNU Toolchain 8-2019-q3**. There is no
lint step and no test runner beyond one host-compiled unit test.

Windows (Docker, produces `dist/`):

```bat
buildXF16Cam.bat ptz      REM or: buildXF16Cam.bat no_ptz
```

Linux/CI-equivalent, with `arm-none-eabi-gcc` on `PATH`:

```sh
printf '%s\n' '__CONFIG_CHIP_TYPE ?= xr872' '__CONFIG_HOSC_TYPE ?= 40' > .config
chmod +x tools/mkimage
make -C project/example/xf16cam/gcc \
  CC_DIR="$(dirname "$(command -v arm-none-eabi-gcc)")" \
  PRJ_EXTRA_SYMBOLS="" image      # add -DNO_PTZ for the fixed-camera board
make -C project/example/xf16cam/gcc \
  CC_DIR="$(dirname "$(command -v arm-none-eabi-gcc)")" \
  PRJ_EXTRA_SYMBOLS="" image_xz
```

`.config` is generated (`configure.sh` interactively, or the `printf` above) and
is not committed. Outputs land in `project/example/xf16cam/image/xr872/`:
`xr_system.img` (serial flashing) and `xr_system_img_xz.img` (web OTA). Never
upload the full serial image through the web updater.

`make clean` / `make image` from `project/example/xf16cam/gcc`. Build knobs that
matter live in `gcc/localconfig.mk` (XIP on, JPEG on, PSRAM off, OTA policy) and
`prj_config.h`.

### Test

One host test, run before the firmware build in both CI and the Dockerfile:

```sh
cc -std=c11 -Wall -Wextra -Werror -Iinclude -Iproject/example/xf16cam \
  tests/xf16cam/test_rtsp_parser.c project/example/xf16cam/xf16cam_rtsp_parser.c \
  -o /tmp/xf16cam-rtsp-parser-test && /tmp/xf16cam-rtsp-parser-test
```

The RTSP request parser is deliberately factored out of `main.c` into
`xf16cam_rtsp_parser.c` so it can be compiled and tested on the host. Keep new
pure-logic code testable the same way.

### Gates CI enforces (run these locally before claiming a change fits)

```sh
python3 tools/xf16cam/check_symbol_placement.py \
  --elf project/example/xf16cam/gcc/xf16cam.axf \
  --require-sram xf16cam_http_flash_info --require-sram xf16cam_http_ota \
  --require-xip xf16cam_http_start

python3 tools/xf16cam/check_image_budget.py \
  --config project/example/xf16cam/image/xr872/image_auto_cal.cfg \
  --image-dir project/example/xf16cam/image/xr872
```

Budget minimums: 8 KiB free in the SRAM-loaded app slot, 64 KiB free in each of
the XIP and compressed-OTA areas. The placement check exists because code that
queries or writes flash must not be executing *from* flash — that is why
`xf16cam_http_flash_info` and `xf16cam_http_ota` must stay in SRAM and uninlined.

CI builds both `ptz` and `no_ptz` variants; a change must compile under both.

## Memory discipline

This is a 1 MiB-flash, no-PSRAM target and the budget is genuinely tight. Two
habits are load-bearing:

- Mark new, non-flash-touching functions `__xip_text` so they stay in XIP flash
  instead of consuming the small SRAM-loaded slot. Most of `main.c` and
  `xf16cam_http.c` already is.
- **`__xip_text` moves only the code.** `appos.ld` collects `.rodata` into the
  same `> RAM` output section as `.text`, so every plain string literal —
  `snprintf` format strings especially — lands in the 64 KiB app slot that CI
  only leaves 8 KiB free in, no matter how the enclosing function is marked.
  Only `.xip_rodata` reaches flash. In `xf16cam_http.c` use
  `XF16CAM_HTTP_SEND_LITERAL` for static markup and `XF16CAM_HTTP_FORMAT` for
  format strings; both park the literal in `__xip_rodata`. Measured on the
  `/api/system` response: 520 bytes moved out of the app slot by switching four
  `snprintf` calls to the macro. Reading a format string from XIP is safe —
  only *disabling* flash while executing from it is not.
- There is exactly one **104,046-byte capture arena** holding a single aligned
  ~100 KiB JPEG buffer, and no YUV framebuffer. Frames are captured one at a
  time and each buffer stays immutable until fully transmitted, so a slow client
  can never observe a buffer mid-overwrite. Do not add a second frame buffer.

Sensor register tables live in XIP flash (`xf16cam_sensor_tables.c`, ~40 KB) and
share one retrying SCCB writer. Adding a sensor means adding a descriptor plus a
table — not new code in the camera core.

Task stacks are small and explicitly sized (`XF16CAM_HTTP_STACK_SIZE`,
`XF16CAM_BOARD_STACK_SIZE`, `XF16CAM_AUDIO_STACK_SIZE`,
`XF16CAM_RTSP_CLIENT_STACK`, MJPEG 3 KiB). The System tab reports minimum spare
stack per worker — use it rather than guessing when adding stack pressure.

## Architecture

Boot (`main.c`) brings up the SDK platform (Wi-Fi/lwIP/console), loads config,
probes the sensor, then **releases** camera power. Resources are demand-driven.

- **`xf16cam_config.c`** — settings in a dedicated FDCM sector at 1016 KiB, so
  they survive reflashing. Versioned schema with in-place migration. Also owns
  the `xf16cam_update_begin/end` mutual exclusion that serializes reboot-causing
  operations (OTA, mode change, AP reset).
- **`main.c`** — camera manager (reference-counted PA23 rail shared with the SD
  card), JPEG capture, MJPEG client tasks, and the RTSP/RTP-JPEG server on port
  8554 (RFC 2435, interleaved over RTSP/TCP). One thread per client, up to
  `XF16CAM_MAX_PARALLEL_CLIENTS` (3) per media type, slots claimed with a CAS
  on `active`. A capture lock serializes frame acquisition so one client cannot
  overwrite a buffer another is still reading. In AP mode the DHCP pool is one
  lease, so only one device associates regardless of that limit.
- **`xf16cam_http.c`** — the entire web console and API, generated as string
  literals directly into the socket (no filesystem, no asset bundle). Editing
  the UI means editing C string literals in `xf16cam_http_page()`.
- **`xf16cam_sensor.c` + `xf16cam_sensor_tables.c`** — descriptor registry,
  probe (some sensors need a reset/wake sequence first), and one table-driven
  backend. Each sensor carries a small CSI profile (byte order, PCLK/HREF/VREF
  polarity, sync type). `xf16cam_sensor_switch_cam_sensor_mode()` is the
  exception to the table-driven rule: it is SP0A39-only and no-ops on every
  other sensor. Night mode is sticky across a reload via `g_sensor_night_mode`.
- **`xf16cam_board.c`** — buttons, LEDs, and the `NO_PTZ` split: PTZ boards use
  PB20 (white LED), PA22 (IR), PB19 (reset) and have no mode button; NO_PTZ
  boards use PA21 (status), PA15 (mode), PA20 (reset). Its poll loop also runs
  two automatic behaviors: **capture-stall recovery** (reboot if clients are
  connected and `last_frame_ms` is 30 s stale — the older unconditional 2-hour
  reboot is commented out, leave it that way) and, on PTZ builds, **day/night
  switching** from a CDS sensor on ADC5 every 5 s.
- **`xf16cam_audio.c`** — on-demand AMIC capture; publishes PCMU silence during
  the 2.1 s analogue settling window so the media clock stays intact.
- **`xf16cam_storage.c`**, **`xf16cam_power.c`**, **`xf16cam_ptz.c`**,
  **`xf16cam_net.c`**, **`xf16cam_rail.c`** — SD card, PA16 battery ADC and
  hibernation, PTZ motion, Wi-Fi bring-up, shared rail refcount.

The DHCP hostname is `XF16CAM-<last three eFuse MAC bytes>`, built once in
`xf16cam_net_start()` before `net_switch_mode()` and handed to the SDK's
`ethernetif_set_hostname()`. It is deliberately not configurable: the eFuse MAC
makes it unique per board from a single firmware image, with no provisioning
step and nothing an OTA or a reflash can overwrite. Adding an editable name
would mean growing `XF16CamConfig`, and `xf16cam_config_storage_valid()` rejects
any record whose `length` differs from `sizeof(XF16CamConfig)` — so that change
needs a real schema-3 migration or it silently resets every device to AP mode.
Flash is fully allocated (settings at 1016 KiB, SDK sysinfo at 1020 KiB), so
there is no spare sector to store it in separately.

A missing or unsupported sensor is non-fatal by design: Wi-Fi, HTTP, OTA, audio,
SD, and diagnostics all stay up while video reports the camera offline. Preserve
that property.

### Media mode is exclusive

`media_mode` is persisted config (`XF16CAM_MEDIA_WEB` or `XF16CAM_MEDIA_RTSP`)
and switching it **reboots**. Browser MJPEG and RTSP do not run concurrently —
an earlier attempt at simultaneous modes was reverted. Do not reintroduce it
without addressing the single-buffer capture constraint.

## Web UI

One responsive page at `http://<device-ip>/`, four tabs — **Live**, **Network**,
**Storage**, **System** — rendered server-side into the socket, with small
inline JS helpers (`tab()`, `scan()`, `toggleAudio()`, `videoStop()`,
`measurePower()`, `update()`).

- **Live** — embedded MJPEG `<img>` (or an "RTSP mode" placeholder), Listen
  button for browser audio, Browser-video/RTSP mode switch, and — PTZ builds
  only — white-LED and IR-LED toggles plus a 5-way PTZ pad. Note the IR toggle
  is not purely manual: it also drives SP0A39 night mode, and on PTZ builds the
  automatic CDS check can revert a manual toggle within 5 seconds.
- **Network** — Wi-Fi scan, SSID/password form, and "Return to open AP". A blank
  password for the currently saved secured SSID preserves the stored credential;
  a blank password for a *different* SSID means an open network.
- **Storage** — SD mount status, total/free space, check, safe eject, format
  FAT32.
- **System** — flash identity, pin map, uptime, boot cause, chip temperature,
  heap headroom, per-worker minimum spare stack, media/audio counters,
  resolution switch (QVGA to native VGA where the sensor supports it), battery
  voltage measurement, hibernate (NO_PTZ builds only), and streamed OTA upload.

## HTTP API

Streams: `GET /stream.mjpeg`, `GET /stream.pcmu` (both 3-client capped, 503 when
full). RTSP: `rtsp://<device-ip>:8554/stream` — force TCP transport in VLC, or
`-rtsp_transport tcp` with ffplay.

JSON reads: `GET /api/scan`, `/api/audio`, `/api/led`, `/api/ir_led`, and
`/api/system` (every System/Live/Storage diagnostic in one streamed response).

Form-encoded writes: `POST /api/wifi`, `/api/ap`, `/api/media`, `/api/resolution`
(these reboot), `/api/led`, `/api/ir_led`, `/api/ptz` (`mode=up|down|left|right|home`,
501 under `NO_PTZ`), `/api/power` (measure battery), `/api/hibernate` (the
inverse: 501 unless `NO_PTZ`),
`/api/sd/refresh`, `/api/sd/eject`, `/api/sd/format`, `/api/reboot` (both
variants; 409 while an update holds the lock, no media quiesce), and `POST /api/ota`
(streamed image, written to the staging area in 2 KiB pieces and only selected
by the bootloader after SDK structure + MD5 verification, so a failed upload
leaves the running firmware bootable).

Handlers return `XF16CAM_HTTP_KEEP_RUNNING` or signal a reboot; errors go through
`xf16cam_http_message(fd, status, text)`.

**The setup AP is unauthenticated by design** and so is the API — anyone in radio
range can reach every endpoint above while AP mode is active. Keep that in mind
before adding anything more destructive than what is already exposed.

## Serial console

Recovery path, kept working independently of media/storage/config state
(`command.c`): `wifi ap`, `wifi sta <ssid> <password>` (saves the same config as
the web page, reboots, never echoes the password), and `upgrade` — an
unconditional BootROM handoff. Responses are `<ACK> <code> <text>`.
`PRJCONF_CONSOLE_EN` must stay enabled.

## Versioning

Bump `XF16CAM_VERSION` in `project/example/xf16cam/xf16cam_version.h`; CI parses
it out of that file to name artifacts, and the build fails if it is empty.
`ChangeLog.md` is the vendor SDK changelog, not the XF16Cam one — do not add
XF16Cam entries there.
