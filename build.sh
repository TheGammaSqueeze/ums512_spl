#!/usr/bin/env bash
#
# Build the ums512_1h10 (Unisoc sharkl5pro) SPL from source and wrap it into a
# flashable spl_a style image (512 byte DHTB header + payload, padded to 4 MiB).
#
# Usage:
#   ./build.sh [nosec|secure|both]     (default: both)
#
# Environment overrides:
#   CROSS_COMPILE   cross compiler prefix   (default: aarch64-linux-gnu-)
#   PART_SIZE       output partition size   (default: 4194304, i.e. 4 MiB)
#   PACKER          dhtb | imgheaderinsert  (default: imgheaderinsert)
#
# Output goes to out/<variant>/spl_a_<variant>.img (and spl_b is a byte copy,
# matching how the two slots hold identical SPLs on this device).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
CHIPRAM_DIR="$REPO_DIR/chipram"
BOARD="ums512_1h10"

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
PART_SIZE="${PART_SIZE:-4194304}"
PACKER="${PACKER:-imgheaderinsert}"

VARIANTS="${1:-both}"
case "$VARIANTS" in
	nosec)  VARIANTS="nosec" ;;
	secure) VARIANTS="secure" ;;
	both)   VARIANTS="nosec secure" ;;
	*) echo "usage: $0 [nosec|secure|both]"; exit 2 ;;
esac

# --- sanity checks ----------------------------------------------------------
if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
	echo "error: ${CROSS_COMPILE}gcc not found. Run scripts/setup-toolchain.sh first." >&2
	exit 1
fi
for t in make python3; do
	command -v "$t" >/dev/null 2>&1 || { echo "error: $t not found"; exit 1; }
done

build_one() {
	local variant="$1"
	local out="$REPO_DIR/out/$variant"
	local frag="$REPO_DIR/board/${BOARD}.${variant}.config"

	echo "==================================================================="
	echo " building SPL variant: $variant"
	echo "==================================================================="
	rm -rf "$out"
	mkdir -p "$out"

	# Configure. BUILD_DIR must be set so 'distclean' targets the out tree and
	# never the source tree (a bare 'make distclean' expands to rm -rf * here).
	make -C "$CHIPRAM_DIR" O="$out" CROSS_COMPILE="$CROSS_COMPILE" distclean >/dev/null
	make -C "$CHIPRAM_DIR" O="$out" CROSS_COMPILE="$CROSS_COMPILE" "${BOARD}_config" >/dev/null

	# Append the board config fragment, mirroring AndroidChipram.mk.
	cat "$frag" >> "$out/include/config.h"

	# Compile.
	make -C "$CHIPRAM_DIR" O="$out" CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)"

	local payload="$out/nand_spl/u-boot-spl-16k.bin"
	[ -f "$payload" ] || { echo "error: SPL payload not produced: $payload"; exit 1; }

	# Wrap with DHTB header and pad to the partition size.
	local img="$out/spl_a_${variant}.img"
	if [ "$PACKER" = "dhtb" ]; then
		python3 "$REPO_DIR/scripts/dhtb_pack.py" "$payload" "$img" "$PART_SIZE"
	else
		# imgheaderinsert writes <name>.img next to its input.
		cp "$payload" "$out/spl_${variant}.bin"
		"$REPO_DIR/tools/imgheaderinsert" "$out/spl_${variant}.bin" 1 >/dev/null
		python3 - "$out/spl_${variant}.img" "$img" "$PART_SIZE" <<'PY'
import sys
src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
d = open(src, "rb").read()
if len(d) > size:
    sys.exit("error: DHTB image %d larger than partition %d" % (len(d), size))
open(dst, "wb").write(d + b"\x00" * (size - len(d)))
PY
	fi
	cp "$img" "$out/spl_b_${variant}.img"

	echo
	echo "  payload : $payload ($(stat -c%s "$payload") bytes)"
	echo "  image   : $img ($(stat -c%s "$img") bytes)"
	echo
}

for v in $VARIANTS; do
	build_one "$v"
done

echo "done. Images:"
find "$REPO_DIR/out" -name 'spl_[ab]_*.img' -printf '  %p\n' | sort
