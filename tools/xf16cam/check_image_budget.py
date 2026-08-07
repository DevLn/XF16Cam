#!/usr/bin/env python3
"""Check XF16Cam artifacts against the configured 1 MiB flash layout."""

import argparse
import json
import re
import sys
from pathlib import Path


SIZE_RE = re.compile(r"^(\d+)([KkMm]?)$")


def parse_size(value):
	if isinstance(value, int):
		return value
	match = SIZE_RE.fullmatch(str(value).strip())
	if not match:
		raise ValueError("invalid size {!r}".format(value))
	multiplier = {"": 1, "k": 1024, "m": 1024 * 1024}[match.group(2).lower()]
	return int(match.group(1)) * multiplier


def usage_line(name, used, limit):
	free = limit - used
	percent = (used * 100.0 / limit) if limit else 0.0
	return "{:<20} {:>9,} / {:>9,} bytes  {:>5.1f}%  {:>9,} free".format(
		name, used, limit, percent, free
	)


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--config", required=True, type=Path,
					help="mkimage primary-image JSON configuration")
	parser.add_argument("--image-dir", required=True, type=Path,
					help="directory containing built image artifacts")
	parser.add_argument("--flash-size", default="1M",
					help="physical flash capacity (default: 1M)")
	parser.add_argument("--reserved-tail", default="8K",
					help="tail reserved for settings and sysinfo (default: 8K)")
	parser.add_argument("--app-reserve", default="8K",
					help="minimum free app-slot space (default: 8K)")
	parser.add_argument("--xip-reserve", default="64K",
					help="minimum free XIP-slot space (default: 64K)")
	parser.add_argument("--ota-reserve", default="64K",
					help="minimum free OTA payload space (default: 64K)")
	args = parser.parse_args()

	try:
		config = json.loads(args.config.read_text(encoding="utf-8"))
		flash_size = parse_size(args.flash_size)
		reserved_tail = parse_size(args.reserved_tail)
		app_reserve = parse_size(args.app_reserve)
		xip_reserve = parse_size(args.xip_reserve)
		ota_reserve = parse_size(args.ota_reserve)
		image_limit = parse_size(config["image"]["max_size"])
		xz_limit = parse_size(config["image"]["xz_max_size"])
		ota_addr = parse_size(config["OTA"]["addr"])
		ota_header_size = parse_size(config["OTA"]["size"])
	except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
		print("image budget: {}".format(error), file=sys.stderr)
		return 2

	sections = []
	for section in config.get("section", []):
		if not section or "bin" not in section or "flash_offs" not in section:
			continue
		try:
			sections.append((parse_size(section["flash_offs"]), section["bin"]))
		except ValueError as error:
			print("image budget: {}".format(error), file=sys.stderr)
			return 2
	sections.sort()

	failures = []
	section_reserves = {"app.bin": app_reserve, "app_xip.bin": xip_reserve}
	if config.get("count") != len(sections):
		failures.append("section count is {}, but configuration declares {}".format(
			len(sections), config.get("count")
		))

	print("XF16Cam flash budget ({} bytes physical flash)".format(f"{flash_size:,}"))
	for index, (offset, filename) in enumerate(sections):
		end = sections[index + 1][0] if index + 1 < len(sections) else image_limit
		limit = end - offset
		artifact = args.image_dir / filename
		if limit <= 0:
			failures.append("{} has an invalid partition range".format(filename))
			continue
		if not artifact.is_file():
			failures.append("missing artifact {}".format(artifact))
			continue
		used = artifact.stat().st_size
		print(usage_line(filename, used, limit))
		if used > limit:
			failures.append("{} exceeds its partition by {:,} bytes".format(
				filename, used - limit
			))
		reserve = section_reserves.get(filename, 0)
		if limit - used < reserve:
			failures.append("{} leaves {:,} bytes free; at least {:,} are required".format(
				filename, limit - used, reserve
			))

	primary_gap = ota_addr - image_limit
	if primary_gap < 0:
		failures.append("primary image limit overlaps OTA metadata by {:,} bytes".format(
			-primary_gap
		))
	else:
		print("primary/OTA gap     {:>9,} bytes".format(primary_gap))

	ota_payload = args.image_dir / "image.xz"
	if not ota_payload.is_file():
		failures.append("missing artifact {}".format(ota_payload))
	else:
		ota_used = ota_payload.stat().st_size
		print(usage_line("image.xz (OTA)", ota_used, xz_limit))
		if ota_used > xz_limit:
			failures.append("image.xz exceeds the OTA payload area by {:,} bytes".format(
				ota_used - xz_limit
			))
		if xz_limit - ota_used < ota_reserve:
			failures.append("image.xz leaves {:,} bytes free; at least {:,} are required".format(
				xz_limit - ota_used, ota_reserve
			))

	ota_end = ota_addr + ota_header_size + xz_limit
	actual_tail = flash_size - ota_end
	print("settings/sysinfo tail {:>9,} / {:>9,} bytes reserved".format(
		actual_tail, reserved_tail
	))
	if actual_tail < reserved_tail:
		failures.append("OTA layout leaves only {:,} bytes of the required {:,}-byte tail".format(
			actual_tail, reserved_tail
		))
	if ota_end > flash_size:
		failures.append("OTA layout exceeds physical flash by {:,} bytes".format(
			ota_end - flash_size
		))

	packaged_limits = {
		"xr_system.img": image_limit,
		"xr_system_img_xz.img": xz_limit,
	}
	for filename, limit in packaged_limits.items():
		artifact = args.image_dir / filename
		if artifact.is_file():
			used = artifact.stat().st_size
			print("{:<20} {:>9,} bytes packaged".format(filename, used))
			if used > limit:
				failures.append("{} exceeds its packaged-image limit by {:,} bytes".format(
					filename, used - limit
				))

	if failures:
		for failure in failures:
			print("ERROR: {}".format(failure), file=sys.stderr)
		return 1
	print("Image budget check passed.")
	return 0


if __name__ == "__main__":
	sys.exit(main())
