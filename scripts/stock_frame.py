#!/usr/bin/env python3
"""Stamp the stock DHTB + SIMGHDR framing onto a sprd_sign output.

The sprd_sign in tools/ (2020 BSP) produces a valid rsa2048_0 signature but
leaves the DHTB header and the SIMGHDR block magic incomplete compared with the
framing the newer factory tool writes. The BootROM needs that framing. All of
these fields live OUTSIDE the signed region:

  - The RSA signature and the SIMGHDR stored hash both cover only the code,
    which is payload[0:f30]. The SIMGHDR block sits at offset f30, after the
    hashed region, so its magic is not part of the signed data.
  - The DHTB header is metadata the BootROM reads, it is not signed.

Therefore stamping these fields does not affect the signature. Field values were
derived from the stock spl_a.img and cross checked against uboot_a.img:

  DHTB 0x08   SHA256 over payload[0:f30]         download integrity hash
  DHTB 0x28   cc cc cc cc aa aa aa aa            constant marker words
  DHTB 0x3c   f30                                code size (SPL image)
  DHTB 0x40   f30 + 0x494                        constant, verified on
                                                 spl  0xed50 -> 0xf1e4 and
                                                 uboot 0xda420 -> 0xda8b4
  payload+f30 "SIMGHDR\\0"                       SIMGHDR block magic

Usage: stock_frame.py <signed.bin>   (edits the file in place)
"""
import sys
import struct
import hashlib

DHTB_HEADER_SIZE = 512
F40_DELTA = 0x494
MARKERS = bytes.fromhex("ccccccccaaaaaaaa")
SIMGHDR_MAGIC = b"SIMGHDR\x00"


def frame(path: str) -> None:
    d = bytearray(open(path, "rb").read())
    if d[:4] != b"DHTB":
        sys.exit("error: %s is not a DHTB image" % path)

    f30 = struct.unpack_from("<I", d, 0x30)[0]
    if f30 == 0 or DHTB_HEADER_SIZE + f30 > len(d):
        sys.exit("error: bad code size f30=%#x" % f30)

    code = bytes(d[DHTB_HEADER_SIZE:DHTB_HEADER_SIZE + f30])

    # DHTB integrity hash over the code, and the marker words
    d[0x08:0x28] = hashlib.sha256(code).digest()
    d[0x28:0x30] = MARKERS
    # size fields
    struct.pack_into("<I", d, 0x3c, f30)
    struct.pack_into("<I", d, 0x40, f30 + F40_DELTA)
    # SIMGHDR block magic (block starts right after the code)
    blk = DHTB_HEADER_SIZE + f30
    d[blk:blk + len(SIMGHDR_MAGIC)] = SIMGHDR_MAGIC

    open(path, "wb").write(d)
    print("framed %s: f30=%#x, 0x40=%#x, SIMGHDR@payload+%#x"
          % (path, f30, f30 + F40_DELTA, f30))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        sys.exit(2)
    frame(sys.argv[1])
