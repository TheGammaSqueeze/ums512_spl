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

## Recommended path: binary-patch the stock SPL

The from-source build below is faithful and educational, but the **reliable**
way to get a working "open" SPL on this device is to binary-patch the stock
`spl_a.img`, because:

- **This device does not validate the SPL's RSA signature.** Proven: the
  on-device working `spl_a_patched.img` carries the *stale* signature copied
  verbatim from stock (identical signature bytes) over *different* code, with
  only the DHTB payload hash recomputed, and it boots. The BootROM checks the
  DHTB hash, not the RSA signature. So no valid signature is needed.
- The stock SPL already does everything correctly (DDR init, the secure-DDR
  firewall, A/B slot selection from misc). The only change needed to run a
  patched or self-built u-boot is to disable the four RSA image checks
  (teecfg / sml / trustos / uboot) it performs before jumping.

`scripts/patch_stock_spl.py` does exactly that: it finds the four
`bl secboot_verify ; cbz w0 ; mov w0,#5 ; bl error` sequences (by the invariant
`cbz w0 ; mov w0,#5` pair), NOPs them, and recomputes the DHTB and SIMGHDR
hashes. Its output is **byte-for-byte identical** to the known-good on-device
`spl_a_patched.img`.

```sh
# you provide the stock spl_a.img pulled from the device or PAC
python3 scripts/patch_stock_spl.py stock_spl_a.img spl_open.img
# flash spl_open.img to both spl_a and spl_b
```

This is the simplest image to flash if you only need stock behaviour with a
patched u-boot. The from-source `signed-open` build below is confirmed booting on
real hardware and is the one to use for the features this tree adds on top of
stock, in particular SD-card boot (see "SD-card boot").

## Layout

```
chipram/        Vendored Unisoc chipram bootloader source (the SPL lives in
                chipram/nand_spl, sharkl5pro board code under
                chipram/nand_spl/board/spreadtrum/sharkl5pro).
board/          Board config fragments appended to the generated config.h,
                one per build variant: nosec, secure, open (used by the
                signed-open modding image), diag (open + on-device diagnostics).
                See "Where the config comes from" below.
tools/          imgheaderinsert, the stock Unisoc DHTB header tool (i386 ELF),
                plus imgheaderinsert_secure, sprd_sign and the signing keys.
scripts/        setup-toolchain.sh, dhtb_pack.py (portable packer),
                stock_frame.py, patch_stock_spl.py.
build.sh        Top level build driver.
```

## Build

```sh
# one time, on Debian or Ubuntu
./scripts/setup-toolchain.sh

# build all four variants (default)
./build.sh

# or a single variant
./build.sh nosec
./build.sh secure
./build.sh signed
./build.sh signed-open
```

Results:

```
out/nosec/spl_a_nosec.img               out/nosec/spl_b_nosec.img
out/secure/spl_a_secure.img             out/secure/spl_b_secure.img
out/signed/spl_a_signed.img             out/signed/spl_b_signed.img
out/signed-open/spl_a_signed-open.img   out/signed-open/spl_b_signed-open.img
```

Flash the variant you want to the `spl_a` and `spl_b` partitions (for example
with fastboot, or by placing it in a PAC). For this device the practical choice
is **`signed-open`**: it is signed with the matching key so a fused unit's
BootROM accepts it, and it does the full secure-world bring-up like stock while
stubbing the RSA image checks so a patched or self-built u-boot boots. This is
the from-source image confirmed booting on real hardware, and the one that
carries the SD-card boot support. Use `nosec` only on a non-fused unit for
reference, and `secure` only on a fused unit where the downstream images are
signed with the matching key, otherwise the SPL will reject them.

### variants

| variant | CONFIG_SECBOOT | RSA image checks | signed | use |
| ------- | -------------- | ---------------- | ------ | --- |
| nosec       | off | n/a          | no  | Reference only. No secure-DDR firewall, will not complete boot on this secure device. |
| secure      | on  | enforced     | no  | Inspection / self-sign. Full secure boot, verifies sml/trustos/teecfg/uboot. |
| signed      | on  | enforced     | yes | Stock equivalent. Firewall + verify; boots only stock (correctly signed) images. |
| signed-open | on  | **stubbed**  | yes | **The modding image.** Full secure-DDR firewall and secure-world bring-up like stock, but the four RSA image checks are stubbed so a patched or self-built u-boot boots. |

Two independent things:

- **Signing the SPL image** (the SIMGHDR + RSA block) decides whether the
  **BootROM accepts the SPL**. Only matters on a fused unit. `rsa2048_0`.
- **`CONFIG_SECBOOT`** turns on the SPL's own secure-world bring-up: the
  `sprd_firewall_config_pre` secure-DDR firewall setup (`sml_teecfg_sec`,
  `dmc_sec`, ...) that the SML/trustos secure world needs, plus the RSA checks of
  the images it loads. The stock SPL is a `CONFIG_SECBOOT` build, so it does this
  firewall setup. A build with secure boot **off** (the old `nosec`/`signed-open`)
  skips it and the SML handoff hangs, which is why those did not boot.

`signed-open` therefore keeps `CONFIG_SECBOOT` **on** (so the firewall/secure
world is set up exactly like stock) but defines `CONFIG_SPL_SKIP_IMG_VERIFY`,
which stubs `secboot_verify()` to always succeed. That skips the four RSA image
checks (sml/trustos/teecfg/uboot) while everything else matches stock, so a
patched u-boot boots.

### Signing (signed variant)

The `signed` variant reproduces the factory signing chain using the stock tools
vendored under `tools/`:

1. build the SPL with the secure config,
2. `tools/imgheaderinsert_secure u-boot-spl-16k.bin 0 0` adds the DHTB header in
   secure mode and produces `u-boot-spl-16k-sign.bin`,
3. `tools/sprd_sign u-boot-spl-16k-sign.bin tools/sign-config pss` appends the
   RSA-2048 signature using the key **rsa2048_0**,
4. pad to 4 MiB.

The signing key is `tools/sign-config/rsa2048_0.pem`. It is the same key the
stock `spl_a.img` was signed with: the modulus embedded in the stock signature
matches `rsa2048_0` exactly. The keys here are Unisoc BSP reference keys,
included at the repository owner's request so the build is self contained.

`tools/sprd_sign` and `tools/imgheaderinsert_secure` load `libc++.so` from
`tools/lib64` via their RUNPATH, so no system libc++ is needed.

**Framing.** The vendored `sprd_sign` (2020 BSP) produces a valid rsa2048_0
signature but leaves the DHTB header and SIMGHDR block magic incomplete compared
with the newer factory tool. `scripts/stock_frame.py` stamps the missing fields
so the container matches the stock image byte for byte in structure:

  - DHTB 0x08: SHA256 over the code (download integrity hash)
  - DHTB 0x28: the `cccccccc aaaaaaaa` marker words
  - DHTB 0x3c: code size, DHTB 0x40: code size + 0x494
  - the `SIMGHDR` magic at the start of the signature block

These fields were derived from the stock `spl_a.img` and cross checked against
`uboot_a.img` (the 0x40 = size + 0x494 relation holds for both). They all sit
outside the signed region: the RSA signature and the SIMGHDR stored hash cover
only the code, and the block sits after it, so stamping them does not touch the
signature. After framing, the only header bytes that differ from the stock image
are the SHA and the size fields, which must differ because the freshly compiled
code differs in size and content.

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

## A/B slot support (reimplemented from the stock binary)

The vendored chipram source predates this device's Android A/B conversion: its
`nand_boot()` loaded non-slotted partitions (`uboot`, `sml`, `trustos`), which do
not exist on this device (the real partitions are `uboot_a`/`uboot_b`,
`sml_a`/`sml_b`, `trustos_a`/`trustos_b`, `teecfg_a`/`teecfg_b`). That is why an
otherwise-correct build did not boot. The stock SPL was fully disassembled and
the missing behaviour reimplemented in `chipram/nand_spl/emmc_boot.c` to match it:

- **Slot selection** (`spl_select_slot`): reads the `misc` partition and parses
  the Android `bootloader_control` block at offset 0x800 (magic `0x42414342`),
  choosing the highest-priority bootable slot. On this device that resolves to
  slot **B**, matching the BCB's stored suffix. Verified byte-for-byte against
  the stock selector and against the real `misc` dump.
- **Slotted names**: `teecfg_<slot>`, `sml_<slot>`, `trustos_<slot>`,
  `uboot_<slot>` are loaded in the stock order (teecfg, sml, trustos, then uboot
  last).
- **Load addresses** taken from the stock binary: SML `0x94000000`, TRUSTOS
  `0x94060000` (the vendored config had the wrong `0x94020000`), TEECFG
  `0x94040000`, u-boot `0x9f000000`.
- **SML handoff ABI**: enters SML at `0x94000000` with `x0` = trustos base
  (`0x94060000`) and `x1` = teecfg base (`0x94040000`), exactly like stock.
- **Dual-backup removed** (`#undef CONFIG_DUAL_BACKUP`): stock has no `*_bak`
  partitions, and the old path ran an `sprd_hash_check` on u-boot that would have
  rejected a patched u-boot. The remaining secure-path fallbacks now target the
  other A/B slot instead of `*_bak`.

If `misc`/BCB is unreadable the selector falls back to the BCB's advisory suffix,
and if that is unavailable it defaults to slot B (this device's shipped active
slot); both slots always exist, so it never targets a missing partition.

Not reimplemented (deliberate, not boot-blocking): the Virtual-A/B
`merge_status` snapshot-merge fast path, and the teecfg-derived
`sprd_firewall_config_attr` sizing on the secure variant (the vendored
`get_tos_size` uses an older teecfg layout that does not match this device).

## SD-card boot

The SPL can boot u-boot from an external microSD card instead of eMMC. On every
boot it loads the secure images (teecfg/sml/trustos) from eMMC as usual, then
tries the SD card for u-boot first, and only if no valid image is found there
does it fall back to the eMMC `uboot_<slot>` partition. This is gated by
`CONFIG_SD_BOOT` (defined in `chipram/include/configs/ums512_1h10.h`, on for this
board) and implemented in `spl_load_uboot_from_sd()` in
`chipram/nand_spl/emmc_boot.c`.

**Placing u-boot on the card.** Write a plain DHTB-wrapped u-boot to the raw
card starting at **sector 200** (byte offset `0x19000`):

- sector 200: the 512 byte DHTB header (`"DHTB"` magic `0x42544844` at offset 0,
  payload length at offset `0x30`),
- sector 201 onward: the raw u-boot payload.

The image on the card is **not** signature/cert checked: a DHTB-wrapped u-boot
whose magic is present is treated as valid and loaded (exactly `mImgSize` bytes,
no trailing cert), so it must be under `CONFIG_UBOOT_MAX_SIZE` (1 MiB). A card
with no valid DHTB image at sector 200 simply falls through to eMMC, so an
inserted data card does not break booting.

**Why the SPL does the full SD bring-up.** The SPL itself is loaded from eMMC, so
the BootROM only ever initializes the eMMC controller; the SD controller (SDIO0
at `0x71100000`) and the slot are completely cold. The SD path therefore performs
all of the controller and slot bring-up that the kernel/u-boot pinctrl would
normally do later:

- **card power**: enable the sc2730 `vddsdcore`/`vddsdio` LDOs. The power
  registers are write-protected, so the power-down clears are silently dropped
  unless the unlock magic (`0x6e7f`) is written first, matching `regulator_init()`
  in `sc27xx_regulator.c`.
- **clock**: set `base_clock` and select the 384 MHz SDIO0 2x source in the AP
  clock core (`REG_AP_CLK_CORE_CGM_SDIO0_2X_CFG`), then divide down to the 400 kHz
  init clock.
- **pads**: the SD0 pads come up in func4, where the controller can read the
  lines but cannot drive CMD/CLK, so they are forced to func1; CMD/DAT pull-ups
  are enabled so the removable-slot lines do not float; and the SDIO0 on-die IO
  supply gate (`BIT_AON_APB_AP_SDIO0_IO_POWER_OFF`) is cleared.
- **PHY delay**: load DLL backup mode plus the board legacy phy-delay so the data
  lines sample correctly when the clock is raised for reads.

This bring-up lives in `SD_HOST_Register()` and `SD_Set_init_clk()` in
`chipram/nand_spl/mmc_v40.c`, guarded by `CONFIG_SOC_SHARKL5PRO` /
`CONFIG_ADIE_SC2730`. Validated on hardware: it boots u-boot straight from the
card, and eMMC boot still works when no card image is present. The binary-patch
path (above) is the stock SPL and does not include SD boot; use the from-source
`signed-open` build for it.

## Secure-world handoff (survives an Android shutdown)

The from-source secure build reproduces the stock SPL's PUB memory-firewall
handoff so that it keeps booting after Android has run and been shut down. An
earlier version sized the SEG_0 secure region as a fixed 32 MB
(`0x94000000..0x95ffffff`), which is broader than stock: stock sizes SEG_0 to the
TEECFG..end-of-TOS extent and leaves the memory above TOS open. A fresh flash
booted fine, but once Android's TOS provisioned memory in the region our
over-broad SEG_0 had marked secure-only, the next boot's TOS faulted and the SML
handoff hung. The fix sizes SEG_0 like stock (via `sml_teecfg_sec` plus a
`tos_sec` call after the teecfg load), reprograms the above-DRAM SEG_7 catch-all
every boot, matches the AON `iram_sec` ranges, and uses the stock 64-bit
`CHIPRAM_ENV` layout. It lives in
`chipram/secure/trustzone/firewall_sharkl5pro.c`.

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

4. To make a fully compatible image on a fused unit, the SPL payload is signed
   with rsa2048_0 using `sprd_sign` (the same step the factory packimage.sh
   runs). This is wired into `build.sh` as the `signed` variant. See "Signing"
   below, including the one known framing difference from the stock image.

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
