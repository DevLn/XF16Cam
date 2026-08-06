# XF16Cam RTSP camera

This isolated example turns the XF16/XR872ET camera JPEG path into a
single-client RTSP stream without PSRAM or an SD card.

- First boot starts setup AP `XF16CAM` / `xf16camera` at `192.168.4.1`.
- Its DHCP server leases `192.168.4.100` to the setup client.
- Open `http://192.168.4.1/` to scan for Wi-Fi and save STA credentials.
- The same page accepts a streamed, verified OTA image and then reboots.
- Saved settings use a dedicated FDCM sector and survive firmware reflashing.
- A failed STA connection falls back to the setup AP.
- Serial recovery: `wifi ap` or `wifi sta <ssid> <password>` saves the same
  configuration as the web page and reboots without exposing the password.
- Stream: `rtsp://<device-ip>:8554/stream`
- The responsive web console has Live, Network, Storage, and System tabs, with
  a focal embedded MJPEG view that can switch exclusively with RTSP mode.
- Browser MJPEG frames are terminated as standalone JPEG images; this avoids
  decoder resynchronization flicker while leaving the RTP/JPEG path unchanged.
- Device information reports the detected flash ID/capacity and known XF16 pin map.
- PA15 short-press switches Web/RTSP mode; PA20 held for three seconds restores
  the setup AP. PA21 blinks during startup and stays on when services are ready.
- An optional one-bit SD card can be mounted and inspected from the web page.
  The page reports total/free space and can explicitly format a card as FAT32.
- Transport: RTP/JPEG (RFC 2435) interleaved over RTSP/TCP
- Image: 320 x 240, JPEG quality 60
- Sensor probing and driver dispatch use a small descriptor registry and one
  register-table backend; adding a compatible sensor does not require changes
  to the shared camera core.

## Camera sensors

- GC0328 (`0x9d`): native XR872 driver, QVGA, hardware validated on XF16.
- GC0308 (`0x9b`): compacted XR872 SDK VGA table, hardware half-scaled to QVGA;
  compiled but awaiting matching-sensor validation.
- HI704 (`0x96`): factory FTY/X5/X6 VGA table, hardware half-scaled to QVGA;
  compiled but awaiting matching-sensor validation.
- SP0A20 (`0x2b`): factory HQT6 VGA table, hardware half-scaled to QVGA;
  compiled but awaiting matching-sensor validation.
- SP0828 (`0x0c`): factory FTY/X5/X6 24 MHz portrait table at 240 x 320;
  compiled but awaiting matching-sensor validation.

The Taixin-derived tables are deliberately limited to byte-exact sequences
corroborated by the supplied factory-firmware research bundle. The alternative
SP0828 tables are not included: the FTY/X5/X6 variant matches the XR872 A9
family and the fixed 24 MHz sensor clock. Register tables live in XIP flash and
share one retrying SCCB writer, avoiding per-sensor code and runtime state.
They were adapted from the camera-driver evidence associated with
`NonPIayerCharacter/OpenTXW81X`; its repository currently has no visible
top-level licence, so provenance should be resolved before redistributing those
three tables beyond this research firmware.

For VLC, force RTSP-over-TCP if it does not select it automatically. For
FFmpeg/ffplay use `-rtsp_transport tcp`.

The capture arena contains two 50 KiB JPEG buffers and no YUV framebuffer.
Camera power, CSI/JPEG, Wi-Fi, and the RTSP listener are initialized only by
this example. `PRJCONF_CONSOLE_EN` remains enabled for serial recovery and
reflashing.

## Confirmed XF16 hardware

- GC0328 CSI: PA0-PA11; camera control: PA14; camera power rail: PA23
- Factory status LED: PA21; mode button: PA15; setup/reset button: PA20
- Microphone: XR872 internal codec analog microphone (AMIC) input, not a GPIO
- SD card: PB16 CMD, PB17 D0, PB18 CLK
- Console: PB0 TX, PB1 RX; SPI flash: PB2-PB7

The button pins and roles were recovered from the factory application's board
configuration. Both inputs are active-low and use internal pull-ups.

## Linux build

The GitHub Actions workflow builds with Arm GNU Toolchain 8-2019-q3 on
Ubuntu 22.04. Each run uploads an `xf16cam-xr872` artifact containing:

- `xf16cam-xr872-v<version>.img`: the complete image for serial flashing
- `xf16cam-xr872-v<version>-ota.img`: compressed image for the web updater
- `SHA256SUMS`: image checksum
- `size.txt`: linked application memory usage

For a local Linux build with `arm-none-eabi-gcc` on `PATH`:

```sh
printf '%s\n' \
  '__CONFIG_CHIP_TYPE ?= xr872' \
  '__CONFIG_HOSC_TYPE ?= 40' > .config
chmod +x tools/mkimage
make -C project/example/xf16cam/gcc \
  CC_DIR="$(dirname "$(command -v arm-none-eabi-gcc)")" \
  -j"$(nproc)" image
make -C project/example/xf16cam/gcc \
  CC_DIR="$(dirname "$(command -v arm-none-eabi-gcc)")" image_xz
```

The flashable result is
`project/example/xf16cam/image/xr872/xr_system.img`; its web-update partner is
`xr_system_img_xz.img` in the same directory.

## 1 MiB flash layout

- `0-32 KiB`: bootloader and reserved space
- `32-86 KiB`: SRAM-loaded application
- `86-560 KiB`: XIP application (about 83 KiB free at v0.11.1)
- `560-598 KiB`: WLAN firmware
- `598-636 KiB`: reserved primary-image growth
- `636-640 KiB`: guard space
- `640-644 KiB`: SDK OTA metadata
- `644-1016 KiB`: compressed, verified OTA staging image
- `1016-1020 KiB`: XF16Cam configuration (FDCM)
- `1020-1024 KiB`: SDK system information

An upload is written directly to the staging area in 2 KiB pieces. It is not
selected by the bootloader until its image structure and MD5 have passed SDK
verification, so a failed or interrupted upload leaves the current firmware
bootable. Do not upload the full serial image through the web page.
