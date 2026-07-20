#!/usr/bin/env python3
"""Make a working "open" SPL by binary-patching the stock spl_a.img.

Why this exists
---------------
This device does NOT validate the SPL's RSA signature. Proof: the on-device
working `spl_a_patched.img` carries the *stale* signature copied verbatim from
the stock image (identical signature bytes) over *different* code, with only the
DHTB payload hash recomputed, and it boots. So the BootROM checks the DHTB hash
but not the RSA signature of the SPL.

The stock SPL is otherwise exactly what this device needs (correct DDR init,
secure-DDR firewall, A/B slot selection from misc, ...). The only change needed
to run a patched or self-built u-boot is to disable the four RSA image checks
(teecfg / sml / trustos / uboot) the SPL performs before jumping. In the binary
those are four identical sequences:

    bl   secboot_verify
    cbz  w0, <ok>          ; 60000034
    mov  w0, #5            ; a0008052
    bl   <error_handler>

This tool finds each of them by the invariant middle pair (cbz w0,.+8 ; mov
w0,#5 == 60000034 a0008052), NOPs the whole 16-byte block, then recomputes the
DHTB header hash (offset 0x08) and the SIMGHDR block's stored hash so the image
is internally consistent. The RSA signature is left as-is (the device ignores
it). The result is byte-for-byte identical to the known-good spl_a_patched.img.

Usage:
    patch_stock_spl.py <stock_spl_a.img> <output.img>

Flash the output to the spl_a and spl_b partitions.
"""
import sys
import struct
import hashlib

NOP = bytes.fromhex("1f2003d5")             # AArch64 NOP (d503201f, little-endian)
VERIFY_FAIL = bytes.fromhex("60000034a0008052")  # cbz w0,.+8 ; mov w0,#5
DHTB_HEADER = 512


def patch(src_path: str, out_path: str) -> None:
    d = bytearray(open(src_path, "rb").read())
    if d[:4] != b"DHTB":
        sys.exit("error: %s is not a DHTB image" % src_path)

    f30 = struct.unpack_from("<I", d, 0x30)[0]       # code size
    code_lo, code_hi = DHTB_HEADER, DHTB_HEADER + f30

    # Find and neutralize every RSA verify-and-fail block within the code.
    hits = []
    i = d.find(VERIFY_FAIL, code_lo, code_hi)
    while i >= 0:
        hits.append(i)
        i = d.find(VERIFY_FAIL, i + 1, code_hi)
    if not hits:
        sys.exit("error: no RSA verify sequences found (unexpected SPL layout)")
    for pos in hits:
        block = pos - 4                              # start at the 'bl verify'
        d[block:block + 16] = NOP * 4

    # Recompute the two hashes so the image is internally consistent. The BootROM
    # checks the DHTB payload hash; the SPL's own loader checks the SIMGHDR hash.
    code = bytes(d[code_lo:code_hi])
    digest = hashlib.sha256(code).digest()
    d[0x08:0x28] = digest                            # DHTB payload hash

    blk = code_hi                                    # SIMGHDR block starts at code end
    j = d.find(bytes.fromhex("00010001"), blk, blk + 0x400)  # RSA exponent marker
    if j < 0:
        sys.exit("error: SIMGHDR pubkey not found")
    stored = j + 4 + 256                             # after modulus
    d[stored:stored + 32] = digest                   # SIMGHDR stored code hash

    open(out_path, "wb").write(bytes(d))
    print("patched %s -> %s: disabled %d RSA checks at code offsets %s"
          % (src_path, out_path, len(hits),
             ", ".join(hex(h - DHTB_HEADER) for h in hits)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        sys.exit(2)
    patch(sys.argv[1], sys.argv[2])
