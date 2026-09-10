#!/usr/bin/env python3
"""Receive the XF16Cam console mirror (XF16CAM_NETLOG builds) on a PC.

The camera broadcasts everything it prints to UDP port 5514. This listens on
that port, timestamps each line, shows which camera it came from, marks
reboots, and appends everything to a log file. No third-party modules.

    python tools/xf16cam/udplog.py
    python tools/xf16cam/udplog.py --file cam.log --port 5514

Windows asks once whether Python may accept incoming connections; allow it on
private networks, or the datagrams never reach the script.
"""
import argparse
import datetime
import socket
import sys
import time

DEFAULT_PORT = 5514
BOOT_MARKER = b"xf16cam version"
PARTIAL_FLUSH_SECONDS = 1.0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--file", default=None,
                        help="log file to append to (default xf16cam-<date>.log)")
    args = parser.parse_args()
    path = args.file or datetime.datetime.now().strftime("xf16cam-%Y%m%d-%H%M%S.log")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", args.port))
    sock.settimeout(0.5)
    print(f"listening on UDP {args.port}, writing {path}; Ctrl+C to stop")

    partial = {}       # source ip -> (bytes, time of last datagram)
    with open(path, "ab") as log:
        def emit(source, line):
            stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            text = line.decode("utf-8", errors="replace").rstrip("\r")
            record = f"{stamp} {source} {text}"
            if BOOT_MARKER in line:
                banner = f"{stamp} {source} ===== camera booted ====="
                print(banner)
                log.write((banner + "\n").encode())
            print(record)
            log.write((record + "\n").encode())
            log.flush()

        while True:
            try:
                data, (source, _) = sock.recvfrom(4096)
            except socket.timeout:
                now = time.monotonic()
                for src, (buffered, last) in list(partial.items()):
                    if buffered and now - last > PARTIAL_FLUSH_SECONDS:
                        emit(src, buffered)
                        partial[src] = (b"", now)
                continue
            buffered, _ = partial.get(source, (b"", 0.0))
            buffered += data
            while b"\n" in buffered:
                line, buffered = buffered.split(b"\n", 1)
                emit(source, line)
            partial[source] = (buffered, time.monotonic())


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(130)
