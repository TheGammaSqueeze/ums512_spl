#!/usr/bin/env bash
#
# Build the ums512_1h10 (Unisoc sharkl5pro) SPL from source and wrap it into a
# flashable spl_a style image (512 byte DHTB header + payload, padded to 4 MiB).
#
# Variants:
#   nosec        CONFIG_SECBOOT off, unsigned. Boots on a unit that is not fused.
#   secure       CONFIG_SECBOOT on, unsigned. For inspection or self signing.
#   signed       CONFIG_SECBOOT on, then RSA-2048 signed with rsa2048_0. Stock
#                equivalent: a fused unit accepts it, but it verifies and so
#                rejects an unsigned or patched u-boot.
#   signed-open  CONFIG_SECBOOT off, then RSA-2048 signed with rsa2048_0. The
#                BootROM accepts it (signature valid) AND the SPL does not verify
#                the next stage, so it boots a patched or unsigned u-boot. Works
#                on both fused and unfused units. This is the modding image.
#
# Usage:
#   ./build.sh [nosec|secure|signed|signed-open|all]     (default: all)
#
# Environment overrides:
#   CROSS_COMPILE   cross compiler prefix   (default: aarch64-linux-gnu-)
#   PART_SIZE       output partition size   (default: 4194304, i.e. 4 MiB)
#   PACKER          dhtb | imgheaderinsert  (default: imgheaderinsert)
#                   Only affects the unsigned variants. Signed always uses the
#                   stock imgheaderinsert + sprd_sign.
#
# Output: out/<variant>/spl_a_<variant>.img and a spl_b copy (both slots hold
# the same SPL on this device).
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
CHIPRAM_DIR="$REPO_DIR/chipram"
BOARD="ums512_1h10"

CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
PART_SIZE="${PART_SIZE:-4194304}"
PACKER="${PACKER:-imgheaderinsert}"

IHI="$REPO_DIR/tools/imgheaderinsert"                 # 2 arg, unsigned DHTB packing
IHI_SECURE="$REPO_DIR/tools/imgheaderinsert_secure"   # 3 arg secure mode, emits -sign.bin
SPRD_SIGN="$REPO_DIR/tools/sprd_sign"
SIGN_CONFIG="$REPO_DIR/tools/sign-config"

VARIANTS="${1:-all}"
case "$VARIANTS" in
	nosec)       VARIANTS="nosec" ;;
	secure)      VARIANTS="secure" ;;
	signed)      VARIANTS="signed" ;;
	signed-open) VARIANTS="signed-open" ;;
	all)         VARIANTS="nosec secure signed signed-open" ;;
	*) echo "usage: $0 [nosec|secure|signed|signed-open|all]"; exit 2 ;;
esac

# --- sanity checks ----------------------------------------------------------
if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
	echo "error: ${CROSS_COMPILE}gcc not found. Run scripts/setup-toolchain.sh first." >&2
	exit 1
fi
for t in make python3; do
	command -v "$t" >/dev/null 2>&1 || { echo "error: $t not found"; exit 1; }
done

pad_to_part() { # <src> <dst>
	python3 - "$1" "$2" "$PART_SIZE" <<'PY'
import sys
src, dst, size = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
d = open(src, "rb").read()
if len(d) > size:
    sys.exit("error: image %d larger than partition %d" % (len(d), size))
open(dst, "wb").write(d + b"\x00" * (size - len(d)))
PY
}

# compile the SPL for a given config fragment; echoes the payload path
compile_spl() { # <out_dir> <config_fragment>
	local out="$1" frag="$2"
	rm -rf "$out"; mkdir -p "$out"
	# BUILD_DIR must be set so 'distclean' targets the out tree, never the source
	# tree (a bare 'make distclean' here expands to rm -rf * and wipes it).
	make -C "$CHIPRAM_DIR" O="$out" CROSS_COMPILE="$CROSS_COMPILE" distclean >/dev/null
	make -C "$CHIPRAM_DIR" O="$out" CROSS_COMPILE="$CROSS_COMPILE" "${BOARD}_config" >/dev/null
	cat "$frag" >> "$out/include/config.h"
	make -C "$CHIPRAM_DIR" O="$out" CROSS_COMPILE="$CROSS_COMPILE" -j"$(nproc)" >/dev/null
	local payload="$out/nand_spl/u-boot-spl-16k.bin"
	[ -f "$payload" ] || { echo "error: SPL payload not produced" >&2; exit 1; }
	echo "$payload"
}

build_variant() {
	local variant="$1"
	local out="$REPO_DIR/out/$variant"
	echo "==================================================================="
	echo " building SPL variant: $variant"
	echo "==================================================================="

	local frag payload img="$out/spl_a_${variant}.img"
	case "$variant" in
	nosec)         frag="$REPO_DIR/board/${BOARD}.nosec.config" ;;   # secboot off, unsigned
	secure|signed) frag="$REPO_DIR/board/${BOARD}.secure.config" ;;  # secboot on, real RSA verify
	signed-open)   frag="$REPO_DIR/board/${BOARD}.open.config" ;;    # secboot on, RSA checks stubbed
	esac

	payload="$(compile_spl "$out" "$frag")"

	if [ "$variant" = "signed" ] || [ "$variant" = "signed-open" ]; then
		# Factory flow: imgheaderinsert in secure mode (arg 0 = secure, 0 = keep
		# original), then sprd_sign with rsa2048_0 (pss), then pad.
		cp "$payload" "$out/u-boot-spl-16k.bin"
		( cd "$out" && "$IHI_SECURE" u-boot-spl-16k.bin 0 0 >/dev/null )
		"$SPRD_SIGN" "$out/u-boot-spl-16k-sign.bin" "$SIGN_CONFIG" pss >/dev/null
		# Stamp the stock DHTB + SIMGHDR framing the BootROM expects. These
		# fields are outside the signed region, so the signature stays valid.
		python3 "$REPO_DIR/scripts/stock_frame.py" "$out/u-boot-spl-16k-sign.bin"
		pad_to_part "$out/u-boot-spl-16k-sign.bin" "$img"
	elif [ "$PACKER" = "dhtb" ]; then
		python3 "$REPO_DIR/scripts/dhtb_pack.py" "$payload" "$img" "$PART_SIZE"
	else
		cp "$payload" "$out/spl_${variant}.bin"
		"$IHI" "$out/spl_${variant}.bin" 1 >/dev/null   # arg 1 = nosec, add payload hash
		pad_to_part "$out/spl_${variant}.img" "$img"
	fi

	cp "$img" "$out/spl_b_${variant}.img"
	echo "  payload : $payload ($(stat -c%s "$payload") bytes)"
	echo "  image   : $img ($(stat -c%s "$img") bytes)"
	echo
}

for v in $VARIANTS; do
	build_variant "$v"
done

echo "done. Images:"
find "$REPO_DIR/out" -name 'spl_[ab]_*.img' -printf '  %p\n' | sort
