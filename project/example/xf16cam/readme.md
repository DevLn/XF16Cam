# XF16 GC0328 RTSP proof

This isolated example turns the working XF16/XR872ET GC0328 JPEG bring-up
into a single-client RTSP stream without PSRAM or an SD card.

- First boot starts setup AP `XF16CAM` / `xf16camera` at `192.168.4.1`.
- Its DHCP server leases `192.168.4.100` to the setup client.
- Open `http://192.168.4.1/` to scan for Wi-Fi and save STA credentials.
- Saved settings use a dedicated FDCM sector and survive firmware reflashing.
- A failed STA connection falls back to the setup AP.
- Serial recovery: `wifi ap` or `wifi sta <ssid> <password>` saves the same
  configuration as the web page and reboots without exposing the password.
- Stream: `rtsp://<device-ip>:8554/stream`
- Transport: RTP/JPEG (RFC 2435) interleaved over RTSP/TCP
- Image: 320 x 240, JPEG quality 60

For VLC, force RTSP-over-TCP if it does not select it automatically. For
FFmpeg/ffplay use `-rtsp_transport tcp`.

The capture arena contains two 50 KiB JPEG buffers and no YUV framebuffer.
Camera power, CSI/JPEG, Wi-Fi, and the RTSP listener are initialized only by
this example. `PRJCONF_CONSOLE_EN` remains enabled for serial recovery and
reflashing.

## Linux build

The GitHub Actions workflow builds with Arm GNU Toolchain 8-2019-q3 on
Ubuntu 22.04. Each run uploads an `xf16cam-xr872` artifact containing:

- `xf16cam-xr872-v<version>.img`: the complete image for serial flashing
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
```

The flashable result is
`project/example/xf16cam/image/xr872/xr_system.img`.
