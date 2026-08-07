# XF16Cam

XF16Cam is a compact network-camera firmware for the 1 MiB-flash
XR872ET/XF16 A9 camera. It provides Wi-Fi AP/STA setup, a mobile-friendly web
console, browser MJPEG or RTSP/RTP-JPEG streaming, AMIC audio, web OTA, and
optional SD-card management without PSRAM.

Start with the [XF16Cam project guide](project/example/xf16cam/readme.md) for
supported sensors, pin assignments, flash layout, build commands, and current
hardware-validation status. GitHub Actions builds both complete serial-flash
and compressed web-OTA images on Linux and checks their 1 MiB memory budgets.

The firmware is implemented as an isolated application over the XRADIO
Skylark SDK, retaining the vendor platform and its original source notices.
OpenXR872/OpenBeken remains a useful integration target; XF16Cam-specific
camera behavior stays within `project/example/xf16cam` wherever possible.

Upstream SDK references:

- [XRADIO Skylark SDK](https://github.com/XradioTech/xradio-skylark-sdk)
- [XRADIO documentation](https://docs.xradiotech.com)
