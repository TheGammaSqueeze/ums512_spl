# ums512_spl

Self contained tooling to build the SPL (first stage / chipram loader, also
called fdl1) for the Unisoc ums512_1h10 (sharkl5pro) platform, and wrap it into
a flashable `spl_a` style image.

The device this targets is the Anbernic style ums512_1h10 board used by the
GammaOS / rotate builds. The output image matches the layout of the stock
`spl_a.img`: a 512 byte DHTB header, the SPL payload, padded with zeros to
4 MiB. The two slots `spl_a` and `spl_b` hold identical SPLs, so the build
writes both.

Everything needed to build is in this repo. The only external dependency is an
aarch64 cross compiler, installed by `scripts/setup-toolchain.sh`.

## Layout

```
chipram/        Vendored Unisoc chipram bootloader source (the SPL lives in
                chipram/nand_spl, sharkl5pro board code under
                chipram/nand_spl/board/spreadtrum/sharkl5pro).
board/          Board config fragments appended to the generated config.h.
                One per variant (nosec, secure). See "Where the config comes
                from" below.
tools/          imgheaderinsert, the stock Unisoc DHTB header tool (i386 ELF).
scripts/        setup-toolchain.sh, dhtb_pack.py (portable packer).
build.sh        Top level build driver.
```

## Build

```sh
# one time, on Debian or Ubuntu
./scripts/setup-toolchain.sh

# build both variants (default)
./build.sh

# or a single variant
./build.sh nosec
./build.sh secure
```

Results:

```
out/nosec/spl_a_nosec.img     out/nosec/spl_b_nosec.img
out/secure/spl_a_secure.img   out/secure/spl_b_secure.img
```

Flash the variant you want to the `spl_a` and `spl_b` partitions (for example
with fastboot, or by placing it in a PAC). Use `nosec` for an unlocked / not
fused unit that boots unsigned or patched u-boot. Use `secure` only on a fused
unit where the downstream images are signed with the matching key, otherwise
the SPL will reject them.

### nosec vs secure

| variant | CONFIG_SECBOOT | behaviour |
| ------- | -------------- | --------- |
| nosec   | off            | SPL loads u-boot, sml, trustos with hash checks only. Boots unsigned images. |
| secure  | on             | SPL RSA verifies loaded images against fused keys. Rejects images not signed with the matching key. |

## Where the config comes from

The board config header `chipram/include/configs/ums512_1h10.h` does not by
itself define the secure memory map (`CONFIG_SML_LDADDR_START`,
`CONFIG_TOS_LDADDR_START`, `CONFIG_SEC_MEM_SIZE`, and the TOS / ATF switches).
In the full Android BSP those are generated at build time by
`chipram/AndroidChipram.mk`, which reads `BOARD_*` variables from the device
tree file `device/sprd/sharkl5Pro/common/security_feature.mk` and appends the
matching `#define` lines to `out/include/config.h`.

This repo reproduces that step. The resolved values for ums512_1h10
(BOARD_TEE_CONFIG=trusty, non feature phone, not low mem) are captured in
`board/ums512_1h10.nosec.config` and `board/ums512_1h10.secure.config`, and
`build.sh` appends the chosen one right after `make ums512_1h10_config`, exactly
as the BSP does.

## DHTB image format

`imgheaderinsert <payload> 1` prepends a 512 byte header:

```
0x000  "DHTB"                      magic
0x004  0x00000001                  version
0x008  SHA256(payload)             32 bytes
0x030  payload length              uint32 little endian
```

The header is followed by the raw payload, then the whole thing is padded with
zeros to the 4 MiB partition size. `scripts/dhtb_pack.py` reproduces this in
pure Python for hosts without 32 bit multilib. Select it with
`PACKER=dhtb ./build.sh`.

## Compatibility with the stock spl_a.img

The stock `spl_a.img` from this device was examined byte for byte. Findings:

1. It is **RSA-2048 signed**. After the SPL code (0xed50 bytes) there is a
   Spreadtrum `SIMGHDR` block (magic `SIMGHDR`) carrying an RSA-2048 public key
   (exponent 0x10001) and a 256 byte signature. The DHTB header fields at
   0x3c and 0x40 describe the signed layout. A plain unsigned build does not
   have this block.

2. The signing key is **rsa2048_0**. The modulus embedded in the stock
   signature block matches `rsa2048_0_pub.pem` in the Unisoc BSP
   (`bsp_build-master/packimage_scripts/config/`) exactly. The private key
   `rsa2048_0.pem` and the signing tool `sprd_sign` are present in that tree,
   so a matching signed image can be produced.

3. **What this means for flashing.** If the target unit has secure boot fused
   in efuse (which is the normal state for a device shipping a signed SPL), the
   BootROM verifies the SPL signature. In that case an **unsigned** image from
   this repo (either variant) will be rejected and will not boot from the spl
   partition. If the unit is **not** fused, the BootROM ignores the signature
   and the unsigned image boots. You must know your unit's fuse state before
   flashing. Recovery from a bad spl is normally still possible through the
   Unisoc BootROM download mode.

4. To make a fully compatible image on a fused unit, the SPL payload must be
   signed with rsa2048_0 using `sprd_sign` (the same step the factory
   packimage.sh runs: `imgheaderinsert` then `sprd_sign <img> <config> pss`).
   That signing step is not wired into `build.sh` here, because it depends on
   vendoring the private key, which is a decision left to the repo owner.

### DDR timing and device tree

The SPL does not use a device tree. All DDR parameters are compile time, taken
from `chipram/include/configs/ums512_1h10.h`:

```
CLK_DDR_FREQ = 1024000000   (1024 MHz)
DDR_MODE     = 0x0002
CFG_DRAM_TYPE = DRAM_LPDDR3
DRAM_SIZE_AUTO_DETECT, DRAM_TYPE_AUTO_DETECT enabled
DCDC_MEM = 1100
```

Because size and type auto detect are enabled, the SPL probes the DRAM at boot
rather than hard coding a single part, which makes it tolerant of DRAM
variations. These values are used exactly as they appear in the vendored
source, so the DDR behaviour matches this source tree. The one thing this repo
cannot guarantee is that the vendored chipram snapshot is the identical
revision Unisoc used to build the stock image; the payload size differs (mostly
due to the gcc 4.9 vs gcc 11 toolchain), but the DDR init path, load addresses
and boot flow are the same.

## Notes on reproducibility

The image is not byte for byte identical to a stock `spl_a.img` built by Unisoc,
because that was compiled with a different (gcc 4.9 Android) toolchain. The
format, load flow, DDR init and boot behaviour are the same. The build here was
verified with gcc-aarch64-linux-gnu 11.4.

### Local changes to the vendored source

`chipram/include/linux/compiler-gcc.h` was patched so that modern gcc (5 and
newer) uses the existing `compiler-gcc4.h` attribute definitions instead of
trying to include a per version header that does not ship in this tree. This is
the only source change; it does not affect generated code.
