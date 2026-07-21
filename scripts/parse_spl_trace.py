#!/usr/bin/env python3
"""
Decode the SPL eMMC state snapshots written by CONFIG_SPL_EMMC_TRACE into the
uboot_log partition, and diff them.

Usage:
    parse_spl_trace.py <uboot_log_dump.bin>

The SPL writes a 2048 byte snapshot right before it jumps to SML, on every boot,
into the always-zero mid-region of uboot_log (1.5 MB in), ringed by reboot count
so successive boots do not overwrite each other and u-boot's own log (first
~16 KB) does not clobber them. This scans the whole dump for snapshots, lists
them ordered by reboot count, and diffs each consecutive pair (a cold boot vs the
failing warm reboot will differ exactly where the stale state is).
"""
import sys
import struct

MAGIC = 0x53504C44                # 'SPLD', stored little-endian -> 44 4C 50 53
MAGIC_LE = bytes([0x44, 0x4C, 0x50, 0x53])

NAMES = {
    0x32290000: "CHIP_RESET_CONTROL_KEY",
    0x32290004: "CHIP_RESET_CONTROL_ENABLE",
    0x32290008: "CHIP_RESET_CONTROL_STATUS",
    0x00015FF8: "DDR_REBOOT_FLAG",
    0x00015FFC: "DDR_REBOOT_CNT",
    0x00003000: "IRAM_DDR_DBG+0x00",
    0x00003004: "IRAM_DDR_DBG+0x04",
    0x00003008: "IRAM_DDR_DBG+0x08",
    0x0000300C: "IRAM_DDR_DBG+0x0C",
    0x00003010: "IRAM_DDR_DBG+0x10",
    0x00003014: "IRAM_DDR_DBG+0x14",
    0x00003018: "IRAM_DDR_DBG+0x18",
    0x0000301C: "IRAM_DDR_DBG+0x1C",
    0x00003400: "IRAM_DDR_DBG2+0x00",
    0x00003404: "IRAM_DDR_DBG2+0x04",
    0x00003408: "IRAM_DDR_DBG2+0x08",
    0x0000340C: "IRAM_DDR_DBG2+0x0C",
    0x327E00F8: "DDR_CHN_SLEEP_CTRL0",
    0x327E00C8: "PUB_SYS_AUTO_LIGHT_SLEEP_ENABLE",
    0x327E012C: "DDR_OP_MODE_CFG",
    0x327E0130: "DDR_PHY_RET_CFG",
    0x327E0250: "PUB_ACC_RDY",
    0x327E0230: "LIGHT_SLEEP_ENABLE",
    0x327E0338: "DDR_SLP_WAIT_CNT",
    0x327E07B8: "DDR_SLP_CTRL_STATUS",
    0x327E00B0: "PMU_APB+0x0B0 (pub soft rst)",
    0x327E0058: "PMU_APB+0x058",
    0x327E00CC: "PMU_APB+0x0CC",
    0x327D0000: "AON_APB_EB0",
    0x327D0004: "AON_APB_EB1",
    0x327D000C: "AON_APB_APB_RST0",
    0x327D0824: "AON_APB_WDG_RST_FLAG",
    0x327D002C: "AON_APB_BOOT_MODE",
    0x31000000: "DMC_CTL0+0x000",
    0x3100000C: "DMC_CTL0+0x00C",
    0x31000100: "DMC_CTL0+0x100 (rst/cke)",
    0x31000104: "DMC_CTL0+0x104",
    0x31001000: "DMC_PHY0+0x000",
    0x31001644: "DMC_PHY0+0x644 (DLL count)",
    0x31053404: "DMC_SOFT_RST_CTRL",
    0x31054004: "DFS_CLK_INIT_SW_START",
    0x31054008: "DFS_CLK_STATE",
    0x3105400C: "DMC_CLK_INIT_CFG",
    0x31054100: "DFS_PURE_SW_CTRL",
    0x31054104: "DFS_SW_CTRL",
    0x31054108: "DFS_SW_CTRL1",
    0x3105410C: "DFS_CLK_INIT_CFG",
    0x31054114: "DFS_HW_CTRL",
    0x32808000: "FW_MST_CTRL_AP RD0",
    0x32808004: "FW_MST_CTRL_AP WR0",
    0x32800064: "FW_SLV_AON0 WR5",
    0x3280C000: "FW_MEM_PUB seg first",
    0x3280C004: "FW_MEM_PUB seg last",
    0x3280C380: "FW_PUB dyn seg first",
    0x3280C384: "FW_PUB dyn seg last",
    0x3280C388: "FW_PUB dyn perm[0]",
    0x3280C38C: "FW_PUB dyn perm[1]",
    0x3280C398: "FW_PUB dyn perm[4]",
    0x3280C39C: "FW_PUB dyn perm[5]",
    0x3280C3A8: "FW_PUB dyn perm[8]",
    0x3280C3B8: "FW_PUB dyn perm[12]",
    0x82000008: "CHIPRAM_ENV+0x08",
    0x82000020: "CHIPRAM_ENV type",
    0x82000028: "CHIPRAM_ENV base",
    0x82000030: "CHIPRAM_ENV size",
    0x20200068: "AP_CLK_CGM_CE_CFG (CE 2x)",
    0x20100000: "AP_AHB_EB",
    0x20100004: "AP_AHB_RST",
    0x20200000: "AP_CLK_CGM base",
    0x327D0008: "AON_APB_EB2",
    0x327D03F0: "AON_APB_DPU2DDR_SLI_LPC",
    0x31001644: "DMC_PHY0+0x644 (DLL count)",
    0xF0000000: "SUM4K(SML @0x94000000)",
    0xF0000001: "SUM4K(TEECFG @0x94040000)",
    0xF0000002: "SUM4K(TOS @0x94060000)",
    0xF0000010: "SUM4K(u-boot @0x9f000000)",
    0xE0000000: "SCTLR_EL3",
    0xE0000001: "SCR_EL3",
    0xE0000002: "VBAR_EL3",
    0xE0000003: "CurrentEL",
    0xE0000004: "DAIF",
}


def name(addr):
    return NAMES.get(addr, "0x%08X" % addr)


def parse_at(data, off):
    if off + 32 > len(data):
        return None
    hdr = struct.unpack_from("<8I", data, off)
    if hdr[0] != MAGIC:
        return None
    ver, boot_type, reboot_cnt, rkey, rstatus, npairs, _ = hdr[1:]
    if npairs > 200:            # sanity guard against a stray magic match
        return None
    regs, order = {}, []
    p = off + 32
    for _ in range(npairs):
        if p + 8 > len(data):
            break
        a, v = struct.unpack_from("<II", data, p)
        p += 8
        if a == 0 and v == 0:
            continue
        regs[a] = v
        order.append(a)
    return {
        "file_off": off, "ver": ver, "boot_type": boot_type,
        "reboot_cnt": reboot_cnt, "reset_key": rkey, "reset_status": rstatus,
        "npairs": npairs, "regs": regs, "order": order,
    }


def find_all(data):
    snaps, i = [], data.find(MAGIC_LE)
    while i != -1:
        if i % 512 == 0:                 # snapshots are sector aligned
            s = parse_at(data, i)
            if s:
                snaps.append(s)
        i = data.find(MAGIC_LE, i + 1)
    return snaps


def wdg(v):
    bits = []
    if v & (1 << 6): bits.append("AP_WDG")
    if v & (1 << 5): bits.append("PCP_WDG")
    if v & (1 << 4): bits.append("WTLCP_WDG")
    return "+".join(bits) if bits else "none"


def classify(s):
    # heuristics only, for labeling; real call is the register diff
    warm_key = (s["reset_key"] == 0x5E486947 and (s["reset_status"] & 3) == 3)
    w = s["regs"].get(0x327D0824, 0)
    if w & 0x70:
        return "WDG-RST(%s)" % wdg(w)
    if warm_key:
        return "WARM(sre)"
    return "boot"


def diff_pair(a, b, la, lb):
    print("--- diff %s vs %s ---" % (la, lb))
    print("%-38s %-12s %-12s" % ("register", la, lb))
    print("-" * 66)
    allregs = list(dict.fromkeys(a["order"] + b["order"]))
    nd = 0
    for r in allregs:
        av, bv = a["regs"].get(r), b["regs"].get(r)
        if av != bv:
            nd += 1
            print("%-38s %-12s %-12s  <-- DIFF"
                  % (name(r),
                     "0x%08X" % av if av is not None else "--",
                     "0x%08X" % bv if bv is not None else "--"))
    if nd == 0:
        print("(identical)")
    print("-> %d differing register(s)\n" % nd)
    return nd


def dump_single(s):
    print("--- snapshot @0x%X (reboot_cnt=%d, %s) ---"
          % (s["file_off"], s["reboot_cnt"], classify(s)))
    for a in s["order"]:
        print("  %-38s 0x%08X" % (name(a), s["regs"][a]))
    print()


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    data = open(sys.argv[1], "rb").read()
    snaps = find_all(data)
    if not snaps:
        print("No SPL trace snapshots found (magic 'SPLD' absent).")
        print("Confirm the diag CONFIG_SPL_EMMC_TRACE image booted at least once.")
        sys.exit(1)

    # order by reboot count, then file offset
    snaps.sort(key=lambda s: (s["reboot_cnt"], s["file_off"]))

    print("Found %d snapshot(s):" % len(snaps))
    for s in snaps:
        print("  @0x%06X ver%d reboot_cnt=%-3d key=0x%08X status=0x%08X "
              "wdg=%s boot_mode=0x%08X -> %s"
              % (s["file_off"], s["ver"], s["reboot_cnt"], s["reset_key"],
                 s["reset_status"], wdg(s["regs"].get(0x327D0824, 0)),
                 s["regs"].get(0x327D002C, 0), classify(s)))
    print()

    if len(snaps) == 1:
        print("Only one snapshot present; need a second boot to diff.\n")
        dump_single(snaps[0])
        return

    # diff each consecutive pair, and first vs last
    for i in range(len(snaps) - 1):
        a, b = snaps[i], snaps[i + 1]
        diff_pair(a, b, "cnt%d" % a["reboot_cnt"], "cnt%d" % b["reboot_cnt"])
    if len(snaps) > 2:
        diff_pair(snaps[0], snaps[-1],
                  "cnt%d" % snaps[0]["reboot_cnt"],
                  "cnt%d" % snaps[-1]["reboot_cnt"])


if __name__ == "__main__":
    main()
