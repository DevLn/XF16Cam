#!/usr/bin/env python3
"""Reject critical XF16Cam symbols placed in unsafe execution regions."""

import argparse
import re
import subprocess
import sys


SYMBOL_RE = re.compile(r"^([0-9a-fA-F]+)\s+\S\s+(.+)$")


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--nm", default="arm-none-eabi-nm")
	parser.add_argument("--elf", required=True)
	parser.add_argument("--require-sram", action="append", default=[])
	parser.add_argument("--require-xip", action="append", default=[])
	parser.add_argument("--require-absent", action="append", default=[],
	                    help="fail if the symbol is linked at all")
	args = parser.parse_args()

	try:
		output = subprocess.check_output([args.nm, "-n", args.elf], text=True)
	except (OSError, subprocess.CalledProcessError) as error:
		print("symbol placement: {}".format(error), file=sys.stderr)
		return 2

	symbols = []
	for line in output.splitlines():
		match = SYMBOL_RE.match(line)
		if match:
			symbols.append((int(match.group(1), 16), match.group(2)))

	failures = []
	for expected, low, high, region in (
		(args.require_sram, 0x00200000, 0x00300000, "SRAM"),
		(args.require_xip, 0x00400000, 0x00500000, "XIP"),
	):
		for prefix in expected:
			matches = [(address, name) for address, name in symbols
			           if name == prefix or name.startswith(prefix + ".")]
			if not matches:
				failures.append("{} is absent (possibly inlined)".format(prefix))
				continue
			for address, name in matches:
				print("{:<48} 0x{:08x} {}".format(name, address, region))
				if not low <= address < high:
					failures.append("{} is at 0x{:08x}, outside {}".format(
						name, address, region
					))

	for name in args.require_absent:
		matches = [(address, symbol) for address, symbol in symbols
		           if symbol == name or symbol.startswith(name + ".")]
		if not matches:
			print("{:<48} {:>10} absent".format(name, "-"))
		for address, symbol in matches:
			failures.append("{} is linked at 0x{:08x} but must be absent".format(
				symbol, address
			))

	if failures:
		for failure in failures:
			print("ERROR: {}".format(failure), file=sys.stderr)
		return 1
	print("Critical symbol placement check passed.")
	return 0


if __name__ == "__main__":
	sys.exit(main())
