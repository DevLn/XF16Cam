# XF16 GC0328 RTSP proof

This isolated example turns the working XF16/XR872ET GC0328 JPEG bring-up
into a single-client RTSP stream without PSRAM or an SD card.

- Wi-Fi mode is selected by `XF16CAM_WIFI_MODE` in `main.c`.
- AP mode: `XF16CAM` / `xf16camera`, address `192.168.51.1`.
- STA bench default: `test` / `1234abcd`, address assigned by DHCP.
- Stream: `rtsp://<device-ip>:8554/stream`
- Transport: RTP/JPEG (RFC 2435) interleaved over RTSP/TCP
- Image: 320 x 240, JPEG quality 60

For VLC, force RTSP-over-TCP if it does not select it automatically. For
FFmpeg/ffplay use `-rtsp_transport tcp`.

The capture arena contains two 50 KiB JPEG buffers and no YUV framebuffer.
Camera power, CSI/JPEG, Wi-Fi, and the RTSP listener are initialized only by
this example. `PRJCONF_CONSOLE_EN` remains enabled for serial recovery and
reflashing.
