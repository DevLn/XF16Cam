#!/usr/bin/env python3
"""Build-time SDK patch for XF16CAM_ESPHOME builds.

lwIP's pool of listening TCP control blocks (MEMP_NUM_TCP_PCB_LISTEN in the
SDK's lwipopts.h, lwIP's own default is 8) is trimmed to 2 by the SDK, and
the HTTP and RTSP servers already hold both, so the ESPHome server's
listen() fails. The define is unguarded and lwIP is compiled from the SDK
tree, so there is no project-side override. Instead of carrying the edit in
the SDK source, the Dockerfile and the CI workflow run this script on
_esphome builds only, right before the firmware build, on their own copy of
the tree. It is idempotent, leaves a pool that is already large enough
alone, and fails when the line is not where it expects it, so an SDK update
is noticed instead of silently reverting to 2. One listen block is about 32
bytes of static RAM.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FILE = ROOT / "include" / "net" / "lwip-1.4.1" / "lwipopts.h"
LINE = re.compile(r"^(#define MEMP_NUM_TCP_PCB_LISTEN[ \t]+)(\d+)([ \t\r]*)$", re.M)
WANT = 3


def main():
	with FILE.open(encoding="utf-8", newline="") as f:
		text = f.read()
	found = LINE.search(text)
	if found is None:
		sys.exit(f"patch_sdk: no MEMP_NUM_TCP_PCB_LISTEN line in {FILE}; "
		         "review the patch after the SDK update")
	have = int(found.group(2))
	if have >= WANT:
		print(f"patch_sdk: MEMP_NUM_TCP_PCB_LISTEN is {have} in {FILE.name}; nothing to do")
		return
	text = text[:found.start(2)] + str(WANT) + text[found.end(2):]
	with FILE.open("w", encoding="utf-8", newline="") as f:
		f.write(text)
	print(f"patch_sdk: MEMP_NUM_TCP_PCB_LISTEN {have} -> {WANT} in {FILE}")


if __name__ == "__main__":
	main()
