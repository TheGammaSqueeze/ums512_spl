#include <config.h>
#include <common.h>
#include <nand.h>
#include <boot_mode.h>
#include <asm/io.h>
//#include "../drivers/mmc/card_sdio.h"
#include <card_sdio.h>
#if defined(CONFIG_SCX35L64) || defined(CONFIG_CHIP_ENV_SET) || defined (CONFIG_WHALE)|| defined (CONFIG_WHALE2)
#include <asm/arch/sprd_chipram_env.h>
#endif
#include <asm/arch/secure_boot.h>
#include "../nand_spl/ufs/simfd_ufs.h"
#include <part.h>
#include <part_efi.h>
#include <security/sec_common.h>
#ifdef CONFIG_SPL_VIBRATE_MARKERS
extern void spl_buzz(int count);   /* haptic progress marker, defined in mcu.c */
#endif
#include <security/trustzone/trustzone.h>
#include <security/sprd_ce_ctrl.h>
#include <adi.h>
#include <asm/arch/sprd_reg.h>
#ifdef CONFIG_TEECFG_CUSTOM
#include "teecfg_parse.h"
#include <security/trustzone/trustzone.h>
#define offset_of(TYPE, MEMBER)      ((size_t)&((TYPE *)0)->MEMBER)
extern void dmc_print_str(const char *string);
#endif
#define SECURE_HEADER_OFF 512


#ifdef CONFIG_DUAL_SPL
/* these parameter used to get the size of uboot.bin from sec header*/
#define SEHEADER_UBOOT_SIZE_OFFSET 0x30
#define BYTE3 0x03
#define BYTE2 0x02
#define BYTE1 0x01
#define BYTE0 0x0
uint8 hash_temp[HASH_TEMP_LEN]={0};
unsigned long uboot_size_no_hash=0;
uint8 *uboot_start_addr = CONFIG_SYS_NAND_U_BOOT_DST;//0x9f00_0000
uint8 *uboot_start_hash_addr = CONFIG_SYS_NAND_U_BOOT_DST-EMMC_SECTOR_SIZE;
uint8 *uboot_size_no_hash_addr=CONFIG_SYS_NAND_U_BOOT_DST-EMMC_SECTOR_SIZE+SEHEADER_UBOOT_SIZE_OFFSET;
BOOLEAN Spl_Hash_Exist(uint8 *p_hash,uint hash_len)
{
	uint32 i;
	for(i=0;i<hash_len;i++)
		{
		if(*(p_hash+i)!=0)
			return TRUE;
		}
	return FALSE;
}

#endif

#ifdef CONFIG_LOAD_PARTITION
int read_common_partition(block_dev_desc_t *dev, uchar * partition_name, uint32_t offsetsector, uint32_t size, uint8_t* buf)
{
	uint32_t count,left;
	disk_partition_t info;

	unsigned char left_buf[dev->blksz];
	count = size/dev->blksz;
	left = size %dev->blksz;

	if (NULL == buf)
		return -1;

	if (get_partition_info_by_name(dev, partition_name, &info))
		return -1;

	if(0!=count)
		if(FALSE == dev->block_read(dev->dev, info.start+offsetsector, count, buf))
			return -1;
	if (left) {
		if (TRUE== dev->block_read(dev->dev, info.start+offsetsector+count, 1, left_buf)){
			sprd_memcpy(buf+(count*dev->blksz), left_buf, left);
		}else{
			return -1;
		}
	}

	return 0;
}

int  load_common_partition(uchar * partition_name,uint32_t size, uint8_t* buf)
{
	int  i, retrys = 5;
	block_dev_desc_t *dev = NULL;
	dev = get_dev();

	if (NULL == dev)
		return -1;

	for(i =0; i<retrys; i++){
	if(0==read_common_partition(dev, partition_name, 0,size, buf))
		      break;
	}

	if(i == 5)
		return -1;

	return 0;
}

#ifdef CONFIG_SPL_EMMC_TRACE
/* ----------------------------------------------------------------------------
 * Diagnostic: persist a verbose snapshot of the SPL's pre-jump hardware state to
 * the uboot_log partition, so a cold-boot dump can be diffed against a
 * warm-reboot dump to localize the warm-reboot hang. The SPL runs to completion
 * on the failing reboot (it writes this, then jumps), so the snapshot survives
 * even when SML/u-boot never come up. Enabled only in the diag build.
 * -------------------------------------------------------------------------- */

/* cold/warm state sampled early in sdram_init, before wdg_rst_keep_sre() runs */
extern volatile u32 g_spl_reset_key;
extern volatile u32 g_spl_reset_status;
extern volatile u32 g_spl_boot_is_warm;

#define SPL_TRACE_MAGIC   0x53504c44   /* 'SPLD' */
#define SPL_TRACE_VER     0x00000005
#define SPL_TRACE_SECTORS 4            /* 2048 bytes per snapshot slot */
/* Store snapshots in the always-zero mid-region of uboot_log (1.5 MB in), well
 * past u-boot's own circular log (first ~16 KB) so a successful boot does not
 * clobber them. Ring of 16 slots keyed by reboot count: consecutive boots land
 * in different slots, so the recovery boot after a failed warm reboot cannot
 * overwrite the failed reboot's snapshot. */
#define SPL_TRACE_BASE_SECTOR 3072     /* 0x180000 / 512 */
#define SPL_TRACE_SLOT_STRIDE 8        /* sectors per slot */
#define SPL_TRACE_NSLOTS      16

/* known-mapped registers captured at the SML handoff (reads are non-disturbing) */
static const uint32_t spl_trace_addrs[] = {
	/* chip reset control (live re-read) + IRAM reboot markers */
	0x32290000, 0x32290004, 0x32290008,
	0x00015FF8, 0x00015FFC,
	/* IRAM DDR debug scratch written by ddrc_init */
	0x00003000, 0x00003004, 0x00003008, 0x0000300C,
	0x00003010, 0x00003014, 0x00003018, 0x0000301C,
	0x00003400, 0x00003404, 0x00003408, 0x0000340C,
	/* PMU_APB sleep / retention / lowpower (0x327e0000 base) */
	0x327e00F8, 0x327e00C8, 0x327e012C, 0x327e0130,
	0x327e0250, 0x327e0230, 0x327e0338, 0x327e07B8,
	0x327e00B0, 0x327e0058, 0x327e00CC,
	/* AON_APB enables + reset cause (0x327d0000 base) */
	0x327d0000, 0x327d0004, 0x327d000C, 0x327d0824, 0x327d002C,
	/* DMC controller status/config (0x31000000 base) */
	0x31000000, 0x31000004, 0x31000008, 0x3100000C, 0x31000010,
	0x31000100, 0x31000104, 0x31000108, 0x3100010C, 0x31000110,
	0x31000120, 0x31000140,
	/* DMC PHY training-result sweep (0x31001000 base): DLL, DQS gate, per-byte
	 * read/write delays computed during training. On a warm reboot DRAM is not
	 * power-cycled, so these can diverge from cold even when DTMG/DLL config match. */
	0x31001000, 0x31001040, 0x31001080, 0x310010C0,
	0x31001100, 0x31001140, 0x31001180, 0x310011C0,
	0x31001200, 0x31001240, 0x31001280, 0x310012C0,
	0x31001300, 0x31001340, 0x31001380, 0x310013C0,
	0x31001400, 0x31001440, 0x31001480, 0x310014C0,
	0x31001500, 0x31001540, 0x31001580, 0x310015C0,
	0x31001600, 0x31001640, 0x31001644, 0x31001680, 0x310016C0,
	/* DDR DVFS / DFS frequency-scaling block (0x31053000/0x31054000). Prime warm
	 * suspect: Android runs DDR DVFS, so a reboot-from-Android enters the SPL with
	 * this hardware in a live scaled state, unlike cold or reboot-from-u-boot. */
	0x31053404,   /* DMC_SOFT_RST_CTRL */
	0x31054004,   /* DFS_CLK_INIT_SW_START */
	0x31054008,   /* DFS_CLK_STATE */
	0x3105400C,   /* DMC_CLK_INIT_CFG */
	0x31054100,   /* DFS_PURE_SW_CTRL */
	0x31054104,   /* DFS_SW_CTRL */
	0x31054108,   /* DFS_SW_CTRL1 */
	0x3105410C,   /* DFS_CLK_INIT_CFG */
	0x31054114,   /* DFS_HW_CTRL */
	/* secure firewall (0x32800000 base) + dynamic PUB catch-all segment
	 * 0x3280c380 (stock programs this every boot from the DRAM-info at
	 * CHIPRAM_ENV 0x82000000; our SPL never touches it, so Android's secure-world
	 * value may persist across a from-Android reboot) */
	0x32808000, 0x32808004, 0x32800064, 0x3280C000, 0x3280C004,
	0x3280C380, 0x3280C384, 0x3280C388, 0x3280C38C,
	0x3280C398, 0x3280C39C, 0x3280C3A8, 0x3280C3B8,
	/* CHIPRAM_ENV DRAM-info struct (0x82000000): stock fills it, we don't */
	0x82000008, 0x82000020, 0x82000028, 0x82000030,
	/* AP + AON clock/reset enables the SPL leaves to Android (dirty on warm,
	 * cold-reset on cold). Read from always-clocked enable regs, not the gated
	 * peripherals themselves. */
	0x20200068,   /* AP_CLK_CGM_CE_CFG (CE 2x) */
	0x20100000,   /* AP_AHB_EB */
	0x20100004,   /* AP_AHB_RST */
	0x20200000,   /* AP_CLK_CGM base */
	0x327d0008,   /* AON_APB_EB2 */
	0x327d03f0,   /* AON_APB_DPU2DDR_SLI_LPC_CTRL */
};

static uint32_t spl_sum32(uint32_t base, uint32_t words)
{
	volatile uint32_t *p = (volatile uint32_t *)(unsigned long)base;
	uint32_t s = 0, i;
	for (i = 0; i < words; i++)
		s = (s * 31u) + p[i];
	return s;
}

static uint32_t spl_rd_sctlr(void)
{ uint64_t v; __asm__ volatile("mrs %0, sctlr_el3" : "=r"(v)); return (uint32_t)v; }
static uint32_t spl_rd_scr(void)
{ uint64_t v; __asm__ volatile("mrs %0, scr_el3" : "=r"(v)); return (uint32_t)v; }
static uint32_t spl_rd_vbar(void)
{ uint64_t v; __asm__ volatile("mrs %0, vbar_el3" : "=r"(v)); return (uint32_t)v; }
static uint32_t spl_rd_currentel(void)
{ uint64_t v; __asm__ volatile("mrs %0, CurrentEL" : "=r"(v)); return (uint32_t)v; }
static uint32_t spl_rd_daif(void)
{ uint64_t v; __asm__ volatile("mrs %0, daif" : "=r"(v)); return (uint32_t)v; }

static int write_common_partition(block_dev_desc_t *dev, uchar *partition_name,
				  uint32_t offsetsector, uint32_t nsectors, uint8_t *buf)
{
	disk_partition_t info;

	if (NULL == buf || NULL == dev)
		return -1;
	if (get_partition_info_by_name(dev, partition_name, &info))
		return -1;
	/* hard bound: never write outside the resolved partition [start, start+size) */
	if ((offsetsector + nsectors) > (uint32_t)info.size)
		return -1;
	if (FALSE == Emmc_Write(PARTITION_USER, (uint32_t)info.start + offsetsector,
				nsectors, buf))
		return -1;
	return 0;
}

void spl_emmc_trace_snapshot(void)
{
	static uint32_t buf[SPL_TRACE_SECTORS * 512 / 4] __attribute__((aligned(64)));
	block_dev_desc_t *dev;
	uint32_t i, idx = 0, n;
	uint32_t slot, off;

	dev = get_dev();
	if (NULL == dev)
		return;

	n = sizeof(spl_trace_addrs) / sizeof(spl_trace_addrs[0]);

	for (i = 0; i < (sizeof(buf) / 4); i++)
		buf[i] = 0;

	buf[idx++] = SPL_TRACE_MAGIC;
	buf[idx++] = SPL_TRACE_VER;
	buf[idx++] = g_spl_boot_is_warm ? 2 : 1;   /* 1 = cold, 2 = warm */
	buf[idx++] = REG32(0x00015FFC);            /* live reboot count */
	buf[idx++] = g_spl_reset_key;              /* early-sampled reset key */
	buf[idx++] = g_spl_reset_status;           /* early-sampled reset status */
	buf[idx++] = n + 9;                        /* total (addr,val) pairs below */
	buf[idx++] = 0;                            /* reserved */

	for (i = 0; i < n; i++) {
		buf[idx++] = spl_trace_addrs[i];
		buf[idx++] = REG32(spl_trace_addrs[i]);
	}

	/* image integrity: 4KB rolling sum of each loaded secure image + u-boot */
	buf[idx++] = 0xF0000000; buf[idx++] = spl_sum32(0x94000000, 1024); /* SML    */
	buf[idx++] = 0xF0000001; buf[idx++] = spl_sum32(0x94040000, 1024); /* TEECFG */
	buf[idx++] = 0xF0000002; buf[idx++] = spl_sum32(0x94060000, 1024); /* TOS    */
	buf[idx++] = 0xF0000010; buf[idx++] = spl_sum32(0x9F000000, 1024); /* u-boot */

	/* CPU EL3 state inherited by SML */
	buf[idx++] = 0xE0000000; buf[idx++] = spl_rd_sctlr();
	buf[idx++] = 0xE0000001; buf[idx++] = spl_rd_scr();
	buf[idx++] = 0xE0000002; buf[idx++] = spl_rd_vbar();
	buf[idx++] = 0xE0000003; buf[idx++] = spl_rd_currentel();
	buf[idx++] = 0xE0000004; buf[idx++] = spl_rd_daif();

	/* Ring by reboot count in the u-boot-safe mid-region (see defines above). */
	slot = REG32(0x00015FFC) & (SPL_TRACE_NSLOTS - 1u);
	off  = SPL_TRACE_BASE_SECTOR + slot * SPL_TRACE_SLOT_STRIDE;

	write_common_partition(dev, (uchar *)"uboot_log", off, SPL_TRACE_SECTORS,
			       (uint8_t *)buf);

	/* restore the eMMC SD clock to the off state the pre-jump path left it in */
	Emmc_DisSdClk();
}
#endif /* CONFIG_SPL_EMMC_TRACE */

#ifdef CONFIG_SPL_DRAM_SCRUB
/* Diagnostic: zero a DRAM region before loading images into it. Tests whether a
 * from-Android reboot hangs in SML because SML reads stale Android data left in
 * DRAM (contents are preserved across the self-refresh-kept warm reset; a cold
 * boot has benign fresh DRAM). Runs with caches off, so stores go straight to
 * DRAM. The SPL itself lives in IRAM, so this cannot touch its own code/stack. */
static void spl_dram_scrub(unsigned long base, unsigned long size)
{
	volatile unsigned long long *p = (volatile unsigned long long *)base;
	unsigned long n = size / sizeof(*p), i;
	for (i = 0; i < n; i++)
		p[i] = 0ULL;
}
#endif

int load_partition_with_header(uchar* partition, uint32_t img_max_size, uint8_t* img_buf, sys_img_header* pHeader)
{
	int  i, retrys = 5;
	block_dev_desc_t *dev = NULL;
	uint32_t image_size;

	dev = get_dev();
	if (NULL == dev)
		return -1;

	/* first read header info */
	for(i = 0; i < retrys; i++){
		if(0==read_common_partition(dev, partition, 0, KEY_INFO_SIZ, pHeader))
			break;
	}
	if(i >= retrys)
		return -1;
	image_size = pHeader->mImgSize;
	#ifdef CONFIG_SECBOOT
	image_size = image_size + 1024; /*secure cert size*/
	#endif
	if (image_size > img_max_size) {
		return -2;
	}

	/* read image */
	for(i = 0; i < retrys; i++){
		if(0==read_common_partition(dev, partition, KEY_INFO_SIZ/dev->blksz,
				image_size, img_buf))
			break;
	}
	if(i >= retrys)
		return -3;

	return 0;
}
#endif

#ifdef CONFIG_DUAL_SPL
int Hash_Check(void)///add the hash_check
{
	uboot_size_no_hash=((*(uboot_size_no_hash_addr+BYTE3))<<24)|((*(uboot_size_no_hash_addr+BYTE2))<<16)|((*(uboot_size_no_hash_addr+BYTE1))<<8)|(*(uboot_size_no_hash_addr+BYTE0));
	sha256_csum_wd(uboot_start_addr,uboot_size_no_hash,hash_temp,NULL);
	if(0==sprd_memcmp(uboot_start_hash_addr+UBOOT_HASH_SIZE_OFFSET,hash_temp,HASH_TEMP_LEN))
	return 0;
	else
	return -1;
}
#endif//end hash_check

/*whale2 and new project,dual backup function  will this interface*/
#ifdef CONFIG_DUAL_BACKUP
/*secboot mode ,no need use this interface*/
#ifndef CONFIG_SECBOOT
BOOLEAN sprd_hash_exist(uint8 *p_hash,uint hash_len)
{
	uint32 i;
	for(i=0;i<hash_len;i++){
		if(*(p_hash+i)!=0)
			return TRUE;
	}
	return FALSE;
}

int8_t sprd_hash_check(uint8_t *header)
{
	uint8 hash_temp[32]={0};
	sys_img_header *img_header = (sys_img_header *)(header);
	if(sprd_hash_exist(img_header->mPayloadHash,32)){//exsit hash
		sha256_csum_wd((uint8_t *)(header+sizeof(sys_img_header)),img_header->mImgSize,hash_temp,NULL);
		if(0 == sprd_memcmp(img_header->mPayloadHash,hash_temp,32))
			return 0;
		else
			return -1;
	}
	return 0;//no need to check hash
}
#endif
#endif

#if defined(CONFIG_WHALE) || defined(CONFIG_WHALE2)
#define TMP_REG_AP_AHB_AHB_EB 0x20210000
#define TMP_BIT_AP_AHB_CC63P_EB (1<<23)
void disable_ahb_cc63_eb()
{
	(*(volatile uint32 *)TMP_REG_AP_AHB_AHB_EB) &= (uint32)(~TMP_BIT_AP_AHB_CC63P_EB);
}
#endif


#define   HWRST_POWERON_MASK 	(0xf0)

/*
* Definition of ANA_RST_STATUS(ANA_REG_GLB_POR_RST_MONITOR) register
* bit[0..7]: Reboot mode
* bit[9]: SYSDUMP enable/disable flag
*/

#define   HWRST_STATUS_RECOVERY                 (0x0020)
#define   HWRST_STATUS_FASTBOOT                 (0x0030)
#define   HWRST_STATUS_NORMAL                   (0x0040)
#define   HWRST_STATUS_ALARM                    (0x0050)
#define   HWRST_STATUS_SLEEP                    (0x0060)
#define   HWRST_STATUS_SPECIAL                  (0x0070)
#define   HWRST_STATUS_CALIBRATION              (0x0090)
#define   HWRST_STATUS_PANIC                    (0x0080)
#define   HWRST_STATUS_AUTODLOADER              (0x00a0)
#define   HWRST_STATUS_IQMODE                   (0x00b0)
#define   HWRST_STATUS_SPRDISK                  (0x00c0)
#define   HWRST_STATUS_NORMAL2                  (0x00f0)
#define   HWRST_STATUS_NORMAL3                  (0x00f1)
#define   HWRST_STATUS_SYSDUMPEN                (0x0200)
#define   HWRST_MOBILEVISOR                     (0x0001)
#define   HWRST_SECURITY                        (0x0002)

int8_t check_sprd_sysdump_sec()
{
#ifdef CONFIG_LOAD_TOS_ALONE
	uint32 reg_rst_moitor;
	uint32 wdt_temp;

	//step1: check if hw wdt rst pending
	ANA_REG_OR(ANA_REG_GLB_ARM_MODULE_EN, BIT_ANA_WDG_EN); //WDG enable
	ANA_REG_OR(ANA_REG_GLB_RTC_CLK_EN,    BIT_RTC_WDG_EN); //WDG Rtc enable

	wdt_temp = ANA_REG_GET(ANA_WDG_BASE+0x10);   //WDG_INT_RAW
	wdt_temp &= 0x8;  //bit3:wdg_rst_raw
	/* don't clear rst/int bit, uboot will read again */

	//step2: read sc2731 RST_MONITOR reg, 0x2E8
	reg_rst_moitor = ANA_REG_GET(ANA_REG_GLB_POR_RST_MONITOR);

	if(! (reg_rst_moitor & HWRST_STATUS_SYSDUMPEN))
		return 0;

	reg_rst_moitor &= 0xFF;

	//setp3: return status by HWRST
	if (wdt_temp) {    //hw wdt rst pending
		if ((reg_rst_moitor == HWRST_SECURITY)) {
			return 1;
		} else {
			return 0;
		}
	}

	return 0;
#else
	return 0;
#endif
}

#ifdef CONFIG_MOBILEVISOR
int8_t bootmode_check_sysdump()
{
	uint32 reg_rst_moitor;
	uint32 wdt_temp;
	uint32 src_flag;

	//step1: check if hw wdt rst pending
	ANA_REG_OR(ANA_REG_GLB_ARM_MODULE_EN, BIT_ANA_WDG_EN); //WDG enable
	ANA_REG_OR(ANA_REG_GLB_RTC_CLK_EN,    BIT_RTC_WDG_EN); //WDG Rtc enable

	wdt_temp = ANA_REG_GET(ANA_WDG_BASE+0x10);   //WDG_INT_RAW
	wdt_temp &= 0x8;  //bit3:wdg_rst_raw
	/* don't clear rst/int bit, uboot will read again */

	//step2: read sc2731 RST_MONITOR reg, 0x2E8
	reg_rst_moitor = ANA_REG_GET(ANA_REG_GLB_POR_RST_MONITOR);

	if(! (reg_rst_moitor & HWRST_STATUS_SYSDUMPEN))
		return 0;

	reg_rst_moitor &= 0xFF;

	//setp3: return status by HWRST
	if (wdt_temp) {    //hw wdt rst pending
		if ( (reg_rst_moitor==HWRST_STATUS_SPECIAL) || (reg_rst_moitor==HWRST_STATUS_PANIC) || (reg_rst_moitor==HWRST_STATUS_NORMAL2) ) {
			return 1;
		} else if ((reg_rst_moitor==HWRST_MOBILEVISOR) || (reg_rst_moitor==HWRST_SECURITY)) {
			return 1;
		} else {
			return 0;
		}
	}

	//step4: 7s reset or ext pin
	src_flag = ANA_REG_GET(ANA_REG_GLB_POR_SRC_FLAG);        //0x2F4
	src_flag &= 0xFFFF;
	if ( (src_flag&0x800) || ((src_flag&0x1080) == 0x1000) ) {   //7s reset
		return 1;
	} else if (reg_rst_moitor==HWRST_STATUS_NORMAL2) {
		return 1;
	} else {
		return 0;
	}
}
#endif

#ifdef CONFIG_TEECFG_CUSTOM
unsigned int get_tos_size(void)
{
	if (sprd_strcmp(TEECFG_HEADER_MAGIC, (unsigned char *)CONFIG_TEECFG_LDADDR_START) != 0) {
		dmc_print_str("teecfg header verify failed!\n");
		return 0;
	}

	return *((unsigned int *)(CONFIG_TEECFG_LDADDR_START + offset_of(tee_config_header_t, tos_size)));
}
#endif

/*
 * A/B slot selection.
 *
 * This device is an Android A/B target: the real partitions are named
 * uboot_a/uboot_b, sml_a/sml_b, trustos_a/trustos_b, teecfg_a/teecfg_b. The
 * active slot is chosen from the Android bootloader_control block (BCB) that
 * lives at byte offset 0x800 inside the misc partition. This mirrors the stock
 * SPL slot selector.
 */
#define BCB_MISC_OFFSET   0x800
#define BCB_MAGIC         0x42414342

struct spl_slot_metadata {
	uint8 byte0;   /* bits0-3 priority, bits4-6 tries_remaining, bit7 successful */
	uint8 byte1;   /* bit0 verity_corrupted / unbootable */
};

struct spl_bootloader_control {
	char   slot_suffix[4];
	uint32 magic;
	uint8  version;
	uint8  nb_and_flags;   /* nb_slots = value & 7 */
	uint8  recovery_tries;
	uint8  merge_status;
	struct spl_slot_metadata slot_info[4];
};

static char  g_slot_suffix[3] = "_a";
static uint8 g_misc_buf[0x1000] __attribute__((aligned(8)));
static uchar g_name_buf[24];

/*
 * Read misc, parse the BCB and return the highest-priority bootable slot:
 * 0 for "_a", 1 for "_b", or -1 if misc/BCB is invalid. Selection rule matches
 * the stock SPL: skip corrupted or out-of-tries slots, then pick by priority,
 * then successful_boot, then tries_remaining.
 */
static int spl_select_slot(void)
{
	block_dev_desc_t *dev = get_dev();
	struct spl_bootloader_control *bcb;
	int i, best = -1, n;

	if (dev == NULL)
		return -1;
	if (0 != read_common_partition(dev, (uchar *)"misc", 0, sizeof(g_misc_buf), g_misc_buf))
		return -1;

	bcb = (struct spl_bootloader_control *)(g_misc_buf + BCB_MISC_OFFSET);
	if (bcb->magic != BCB_MAGIC)
		return -1;
	if (bcb->version > 1)
		return -1;

	n = bcb->nb_and_flags & 7;
	if (n > 4)
		n = 4;

	for (i = 0; i < n; i++) {
		uint8 b0 = bcb->slot_info[i].byte0;
		uint8 b1 = bcb->slot_info[i].byte1;

		if (b1 & 1)                 /* corrupted / unbootable */
			continue;
		if (((b0 >> 4) & 7) == 0)   /* no tries remaining */
			continue;
		if (best < 0) {
			best = i;
		} else {
			uint8 bb = bcb->slot_info[best].byte0;
			int pi = b0 & 0xf, pb = bb & 0xf, diff;

			if (pi != pb) {
				diff = pb - pi;
			} else {
				int si = (b0 >> 7) & 1, sb = (bb >> 7) & 1;

				if (si != sb)
					diff = sb - si;
				else
					diff = ((bb >> 4) & 7) - ((b0 >> 4) & 7);
			}
			if (diff < 0)
				best = i;
		}
	}
	if (best < 0) {
		/* Valid BCB but no slot won on priority: trust the advisory suffix. */
		return (bcb->slot_suffix[1] == 'b') ? 1 : 0;
	}
	return best;
}

/*
 * Build "<base><suffix>" (e.g. "uboot" + "_b") into the shared name buffer.
 * An empty suffix ("") yields the bare base name, which is what a non-A/B
 * (mainline) card uses for its single "uboot"/"sml"/... partitions.
 */
static uchar *spl_slot_name(const char *base, const char *suffix)
{
	int i = 0;

	while (base[i]) {
		g_name_buf[i] = (uchar)base[i];
		i++;
	}
	if (suffix && suffix[0]) {
		g_name_buf[i++] = (uchar)suffix[0];
		if (suffix[1])
			g_name_buf[i++] = (uchar)suffix[1];
	}
	g_name_buf[i] = 0;
	return g_name_buf;
}

/* True if a partition with this name exists on the given device's GPT. */
static int spl_part_exists(block_dev_desc_t *dev, const char *name)
{
	disk_partition_t info;

	return (0 == get_partition_info_by_name(dev, (uchar *)name, &info));
}

/* Suffix of the inactive slot, used as the redundancy fallback on this A/B device. */
static const char *spl_other_suffix(void)
{
	return (g_slot_suffix[1] == 'b') ? "_a" : "_b";
}

#ifdef CONFIG_SD_BOOT
/*
 * Try to load u-boot from a raw SD-card image instead of the eMMC uboot_<slot>
 * partition. Validity confirmed by header magic before the jump.
 */
static BOOLEAN spl_load_uboot_from_sd(void)
{
	sys_img_header *hdr = (sys_img_header *)(CONFIG_SYS_NAND_U_BOOT_DST - KEY_INFO_SIZ);
	uint32 img_size, img_sectors;
	BOOLEAN sd_ok;

#ifdef CONFIG_SPL_VIBRATE_MARKERS
	spl_buzz(7);   /* DIAG: entered SD path, about to call SD_Init */
#endif
	sd_ok = SD_Init();
#ifdef CONFIG_SPL_VIBRATE_MARKERS
	spl_buzz(8);   /* DIAG: SD_Init returned (did NOT hang) */
#endif
	if (TRUE != sd_ok)
		return FALSE;
#ifdef CONFIG_SPL_VIBRATE_MARKERS
	spl_buzz(10);   /* DIAG: SD_Init returned TRUE (a card was detected) */
#endif

	/* header first: KEY_INFO_SIZ bytes at the boot sector */
	if (TRUE != SD_CARD_Read(SDCARD_BOOT_SECTOR, KEY_INFO_SIZ / EMMC_SECTOR_SIZE,
				 (uint8 *)hdr))
		return FALSE;

	if (hdr->mMagicNum != 0x42544844)	/* "DHTB": presence means valid image on this card */
		return FALSE;

	/* no signature/cert check on the SD image: a DHTB-wrapped u-boot present on
	 * the card (magic checked above) is enough. Load exactly the payload; unlike
	 * the eMMC image it carries no trailing secure cert. Bounding mImgSize here
	 * also stops a malformed header from wrapping past the size check. */
	img_size = hdr->mImgSize;
	if (img_size == 0 || img_size > CONFIG_UBOOT_MAX_SIZE)
		return FALSE;

	img_sectors = (img_size + EMMC_SECTOR_SIZE - 1) / EMMC_SECTOR_SIZE;
	if (TRUE != SD_CARD_Read(SDCARD_BOOT_SECTOR + KEY_INFO_SIZ / EMMC_SECTOR_SIZE,
				 img_sectors, (uint8 *)CONFIG_SYS_NAND_U_BOOT_DST))
		return FALSE;

	return TRUE;
}
#endif

void nand_boot(void)
{
	int ret;
	int i, j;
	block_dev_desc_t *dev = NULL;
	disk_partition_t info;
	__attribute__ ((noreturn)) void (*uboot) (void);
	int8_t sysdump_mode = 0, sysdump_security = 0;

#if 0
	unsigned int i = 0;
	for (i = 0xffffffff; i > 0;)
		i--;
#endif
	/*
	 * Init board specific nand support
	 */
#ifdef SPRD_EVM_TAG_ON
#if 0
	unsigned long int *ptr = (unsigned long int *)SPRD_EVM_ADDR_START - 8;
	int ijk = 0;
	for (ijk = 0; ijk < 28; ijk++) {
		*(ptr++) = 0x55555555;
	}
#endif
	SPRD_EVM_TAG(1);
#endif

	sysdump_security = check_sprd_sysdump_sec();
	if (!sysdump_security) {  //sysdump mode (tos panic) don't enable firewall
		/* Don't set up firewall locks here so we can actually check if we need them first.
		 * USB clocks were unconditional here though, so we keep doing that. */ 
		sprd_firewall_usb_clk_enable();
	}

#ifdef CONFIG_EMMC_BOOT
#ifndef CONFIG_LOAD_PARTITION
#ifdef CONFIG_SECURE_BOOT
	if (TRUE == Emmc_Init()) {
		Emmc_Read(PARTITION_BOOT2, 2, (CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM + 1), (uint8 *) (CONFIG_SYS_NAND_U_BOOT_DST - KEY_INFO_SIZ));
		//Emmc_Read(PARTITION_BOOT2, 2, (CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM + 1), (uint8 *)(CONFIG_SYS_NAND_U_BOOT_DST));
		Emmc_Read(PARTITION_BOOT2, 1, 1, (uint8 *) VLR_INFO_OFF);
	}
#else
	if (TRUE == isSDCardBoot()) {
		if (TRUE == SD_Init()) {
			SD_CARD_Read(PARTITION_BOOT2, 0, CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM, (uint8 *) CONFIG_SYS_NAND_U_BOOT_DST);
		} else {

			if (TRUE == Emmc_Init()) {
				Emmc_Read(PARTITION_BOOT2, 0, CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM, (uint8 *) CONFIG_SYS_NAND_U_BOOT_DST);
			}
		}
	} else {
		if (TRUE == Emmc_Init()) {
			Emmc_Read(PARTITION_BOOT2, 0, CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM, (uint8 *) CONFIG_SYS_NAND_U_BOOT_DST);
		}
	}
#endif
#endif
#ifdef CONFIG_LOAD_PARTITION
#ifdef CONFIG_SPL_VIBRATE_MARKERS
		spl_buzz(3);   /* STAGE 3: nand_boot + firewall_config_pre done, about to init eMMC */
#endif
		dev = NULL;
#ifdef CONFIG_SD_BOOT
		/* Prefer a GPT-partitioned SD card when present. SD and eMMC share the
		 * SDIO0 controller, so commit to SD only once we've confirmed it carries a
		 * valid GPT with a u-boot partition (slotted or not) */
		if (TRUE == SD_Init()) {
			block_dev_desc_t *sd = sd_get_dev();
			if (spl_part_exists(sd, "uboot") ||
			    spl_part_exists(sd, "uboot_a") ||
			    spl_part_exists(sd, "uboot_b")) {
				spl_set_boot_dev(sd);
				dev = sd;
			}
		}
#endif
		if (dev == NULL) {
			if (TRUE == Emmc_Init()) {
				spl_set_boot_dev(NULL);   /* get_dev() -> eMMC default */
				dev = get_dev();
			}
		}
#ifdef CONFIG_SPL_VIBRATE_MARKERS
		spl_buzz(4);   /* STAGE 4: boot device initialized */
#endif
		int load_secure;
		if (dev != NULL) {
			BOOLEAN uboot_ok;

			/* A/B slot resolution, but only if the card is actually slotted. */
			if (spl_part_exists(dev, "uboot_a") ||
			    spl_part_exists(dev, "uboot_b")) {
				int slot = spl_select_slot();
				if (slot < 0)
					slot = 1;   /* misc/BCB unreadable: default to the slot this device
					             * ships active (_b). Both slots exist, so this never
					             * targets a missing partition; the BCB normally decides. */
				g_slot_suffix[0] = '_';
				g_slot_suffix[1] = slot ? 'b' : 'a';
			} else {
				g_slot_suffix[0] = 0;   /* no A/B: bare partition names */
			}

			/* Check if we actually need sml handoff, so mainline can boot without it. */
			load_secure = spl_part_exists(dev,
					(const char *)spl_slot_name("sml", g_slot_suffix));

			if (!sysdump_security && load_secure) {  //sysdump mode (tos panic) don't load tos and sml
				/* The previously deferred firewall setup, now that we know we need it. */
				sprd_firewall_config_pre();

#ifdef CONFIG_SPL_DRAM_SCRUB
				/* zero the secure region before loading SML/TOS/TEECFG on top, so
				 * SML never sees Android's stale DRAM on a from-Android reboot */
				spl_dram_scrub(CONFIG_SML_LDADDR_START, CONFIG_SEC_MEM_SIZE);
#endif
#if CONFIG_SMLBOOT// whale
				load_partition_with_header(spl_slot_name("sml", g_slot_suffix), SML_LOAD_MAX_SIZE,CONFIG_SML_LDADDR_START,(sys_img_header*)(CONFIG_SML_LDADDR_START-IMAGE_HEAD_SIZE));
				load_partition_with_header(spl_slot_name("trustos", g_slot_suffix), TOS_LOAD_MAX_SIZE,CONFIG_TOS_LDADDR_START,(sys_img_header*)(CONFIG_TOS_LDADDR_START-IMAGE_HEAD_SIZE));
#endif
#ifdef CONFIG_TEECFG_LDADDR_START
				/* teecfg carries the secure-world memory map; stock loads it first and passes it to SML */
				load_partition_with_header(spl_slot_name("teecfg", g_slot_suffix), TEECFG_LOAD_MAX_SIZE, CONFIG_TEECFG_LDADDR_START, (sys_img_header *)(CONFIG_TEECFG_LDADDR_START - IMAGE_HEAD_SIZE));
#endif
#ifdef CONFIG_LOAD_ATF
				load_partition_with_header(spl_slot_name("sml", g_slot_suffix), SML_LOAD_MAX_SIZE,CONFIG_SML_LDADDR_START,(sys_img_header*)(CONFIG_SML_LDADDR_START-IMAGE_HEAD_SIZE));
#endif
#ifdef CONFIG_LOAD_TOS_ALONE
				load_partition_with_header(spl_slot_name("trustos", g_slot_suffix), TOS_LOAD_MAX_SIZE,CONFIG_TOS_LDADDR_START,(sys_img_header*)(CONFIG_TOS_LDADDR_START-IMAGE_HEAD_SIZE));
#endif
#ifdef CONFIG_SPL_FW_PARITY
				/* stock parity: now that teecfg is loaded, extend PUB firewall seg0
				 * to cover TEECFG..end-of-TOS, using the tos_size from the teecfg
				 * header (offset 0x20). Matches stock's tos_sec after teecfg parse. */
				{
					sprd_fw_attr fw_attr;
					fw_attr.tos_size = *((volatile unsigned int *)(CONFIG_TEECFG_LDADDR_START + 0x20));
					sprd_firewall_config_attr(&fw_attr);
				}
#endif
			}

			/* u-boot: always, from the selected GPT device (SD or eMMC). */
			uboot_ok = (0 == load_partition_with_header(spl_slot_name("uboot", g_slot_suffix),CONFIG_UBOOT_MAX_SIZE,CONFIG_SYS_NAND_U_BOOT_DST,(sys_img_header*)(CONFIG_SYS_NAND_U_BOOT_DST - KEY_INFO_SIZ))) ? TRUE : FALSE;

#ifdef CONFIG_SD_BOOT
			/* Legacy fallback: raw DHTB u-boot at the fixed SD boot sector 2900,
			 * Then restore eMMC (SD reset SDIO0) and possibly fallback to u-boot
			 * from the eMMC A/B slot. */
			if (TRUE != uboot_ok) {
				BOOLEAN sd_raw = spl_load_uboot_from_sd();
				Emmc_Init();
				spl_set_boot_dev(NULL);   /* back to eMMC for the final fallback */
				if (TRUE != sd_raw) {
					int slot;
#ifdef CONFIG_SPL_VIBRATE_MARKERS
					spl_buzz(9);   /* DIAG: SD failed, loading u-boot from eMMC */
#endif
					slot = spl_select_slot();
					if (slot < 0)
						slot = 1;
					g_slot_suffix[0] = '_';
					g_slot_suffix[1] = slot ? 'b' : 'a';
					load_partition_with_header(spl_slot_name("uboot", g_slot_suffix),CONFIG_UBOOT_MAX_SIZE,CONFIG_SYS_NAND_U_BOOT_DST,(sys_img_header*)(CONFIG_SYS_NAND_U_BOOT_DST - KEY_INFO_SIZ));
				}
			}
#endif

#ifdef CONFIG_MOBILEVISOR
		sysdump_mode = bootmode_check_sysdump();
		if (!sysdump_mode) {
			ddr_print_string("\r\nStart load vmm images from emmc\r\n");
			/* reserve KEY_INFO_SIZE for image loading area */
			ret = load_partition_with_header("secvm", CONFIG_SECVM_SIZE - KEY_INFO_SIZ,
				(uint8*)CONFIG_SECVM_ADDR,
				(sys_img_header*)(CONFIG_SECVM_ADDR - KEY_INFO_SIZ));
			if (ret < 0){
				ddr_print_string("load secvm fail\r\n");
				while(1);
			}
			ret = load_partition_with_header("mvconfig", CONFIG_MVCONFIG_SIZE - KEY_INFO_SIZ,
				(uint8*)CONFIG_MVCONFIG_ADDR,
				(sys_img_header*)(CONFIG_MVCONFIG_ADDR - KEY_INFO_SIZ));
			if (ret < 0){
				ddr_print_string("load mvconfig fail\r\n");
				while(1);
			}
			ret = load_partition_with_header("mobilevisor", CONFIG_MOBILEVISOR_SIZE - KEY_INFO_SIZ,
				(uint8*)CONFIG_MOBILEVISOR_ADDR,
				(sys_img_header*)(CONFIG_MOBILEVISOR_ADDR - KEY_INFO_SIZ));
			if (ret < 0){
				ddr_print_string("load mobilevisor fail\r\n");
				while(1);
			}
			ddr_print_string("Load vmm images succ\r\n");
		}
#endif
			#ifdef CONFIG_DUAL_SPL//only for tshark3 dual spl;
			if(Spl_Hash_Exist(uboot_start_hash_addr+UBOOT_HASH_SIZE_OFFSET,HASH_TEMP_LEN))
			{
				if(0!=Hash_Check())
				{
				load_partition_with_header("uboot_bak",CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM*EMMC_SECTOR_SIZE,CONFIG_SYS_NAND_U_BOOT_DST,(sys_img_header*)(CONFIG_SYS_NAND_U_BOOT_DST - EMMC_SECTOR_SIZE));
				if(0!=Hash_Check())
					while(1);
				}
			}
			#endif
		}
#endif
#endif

#ifdef CONFIG_UFS_BOOT
		if(0 == ufs_init()){
		load_partition_with_header("uboot",CONFIG_SYS_UFS_U_BOOT_SECTOR_NUM*UFS_SECTOR_SIZE,CONFIG_SYS_NAND_U_BOOT_DST,(sys_img_header*)(CONFIG_SYS_NAND_U_BOOT_DST - KEY_INFO_SIZ));
		}
#endif

/*whale2 and new project,dual backup function  will this interface*/
#ifdef CONFIG_DUAL_BACKUP
#ifdef CONFIG_SECBOOT
	/*sec boot ,dual-backup*/
#else
	/*no sec boot ,dual-backup*/
#ifdef CONFIG_SMLBOOT
	if(0 != sprd_hash_check((uint8_t*)(CONFIG_SML_LDADDR_START-IMAGE_HEAD_SIZE))){
		load_partition_with_header(spl_slot_name("sml", spl_other_suffix()), SML_LOAD_MAX_SIZE,CONFIG_SML_LDADDR_START,(sys_img_header*)(CONFIG_SML_LDADDR_START-IMAGE_HEAD_SIZE));
		if(0 != sprd_hash_check((uint8_t*)(CONFIG_SML_LDADDR_START-IMAGE_HEAD_SIZE)))
			while(1);//sml hash check fail
	}

	if(0 != sprd_hash_check((uint8_t*)(CONFIG_TOS_LDADDR_START-IMAGE_HEAD_SIZE))){
		load_partition_with_header("trustos_bak", TOS_LOAD_MAX_SIZE,CONFIG_TOS_LDADDR_START,(sys_img_header*)(CONFIG_TOS_LDADDR_START-IMAGE_HEAD_SIZE));
		if(0 != sprd_hash_check((uint8_t*)(CONFIG_TOS_LDADDR_START-IMAGE_HEAD_SIZE)))
			while(1);//tos hash check fail
	}


#endif
	if(0 != sprd_hash_check((uint8_t*)(CONFIG_SYS_NAND_U_BOOT_DST-IMAGE_HEAD_SIZE))){
		load_partition_with_header("uboot_bak",CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM*EMMC_SECTOR_SIZE,CONFIG_SYS_NAND_U_BOOT_DST,(sys_img_header*)(CONFIG_SYS_NAND_U_BOOT_DST - EMMC_SECTOR_SIZE));
		if(0 != sprd_hash_check((uint8_t*)(CONFIG_SYS_NAND_U_BOOT_DST-IMAGE_HEAD_SIZE))){
			/* SD-boot is handled in the active CONFIG_LOAD_PARTITION path
			 * (spl_load_uboot_from_sd), not here: this CONFIG_DUAL_BACKUP
			 * branch is #undef'd on this device. */
		}
	}
#ifdef CONFIG_MOBILEVISOR
/*we now check hash of vmm image*/
	if (!sysdump_mode) {
		if(0 != sprd_hash_check((uint8_t*)(CONFIG_SECVM_ADDR-IMAGE_HEAD_SIZE))){
			while(1);
		}
		if(0 != sprd_hash_check((uint8_t*)(CONFIG_MVCONFIG_ADDR-IMAGE_HEAD_SIZE))){
			while(1);
		}
		if(0 != sprd_hash_check((uint8_t*)(CONFIG_MOBILEVISOR_ADDR-IMAGE_HEAD_SIZE))){
			while(1);
		}
	}
#endif
#endif
#endif

#ifdef CONFIG_SECBOOT

    secboot_init();

	if (!sysdump_security) { //sysdump mode (tos panic) don't verify tos and sml
#if defined(CONFIG_SMLBOOT)|| defined(CONFIG_LOAD_ATF)
		if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_SML_LDADDR_START - IMAGE_HEAD_SIZE),NULL,NULL,SECURE_BOOT)))
		{
			/***secboot sml dual_backup***/
			load_partition_with_header(spl_slot_name("sml", spl_other_suffix()), SML_LOAD_MAX_SIZE,CONFIG_SML_LDADDR_START,(sys_img_header*)(CONFIG_SML_LDADDR_START-IMAGE_HEAD_SIZE));
			if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_SML_LDADDR_START - IMAGE_HEAD_SIZE),NULL,NULL,SECURE_BOOT)))
			{
				while(1);
			}
		}
#endif

#if defined(CONFIG_TEECFG_CUSTOM)
		if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN, (CONFIG_TEECFG_LDADDR_START - IMAGE_HEAD_SIZE), NULL, NULL, SECURE_BOOT)))
		{
			/***secboot teecfg dual_backup***/
			load_partition_with_header("teecfg_bak", TEECFG_LOAD_MAX_SIZE, CONFIG_TEECFG_LDADDR_START, (sys_img_header*)(CONFIG_TEECFG_LDADDR_START - IMAGE_HEAD_SIZE));
			if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN, (CONFIG_TEECFG_LDADDR_START - IMAGE_HEAD_SIZE), NULL, NULL, SECURE_BOOT)))
			{
				while(1);
			}
		}

		sprd_fw_attr fw_attr;
		fw_attr.tos_size = get_tos_size();
		sprd_firewall_config_attr(&fw_attr);
#endif

#if defined(CONFIG_SMLBOOT)|| defined(CONFIG_LOAD_TOS_ALONE)
		if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_TOS_LDADDR_START - IMAGE_HEAD_SIZE),NULL,NULL,SECURE_BOOT)))
		{
			/***secboot tos: fall back to the other A/B slot***/
			load_partition_with_header(spl_slot_name("trustos", spl_other_suffix()), TOS_LOAD_MAX_SIZE,CONFIG_TOS_LDADDR_START,(sys_img_header*)(CONFIG_TOS_LDADDR_START-IMAGE_HEAD_SIZE));
			if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_TOS_LDADDR_START - IMAGE_HEAD_SIZE),NULL,NULL,SECURE_BOOT)))
			{
				while(1);
			}
		}
#endif
	}

#ifdef CONFIG_MOBILEVISOR
	if (!sysdump_mode) {
		if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_SECVM_ADDR - KEY_INFO_SIZ),NULL,NULL,SECURE_BOOT)))
		{
			while(1);
		}

		if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_MVCONFIG_ADDR - KEY_INFO_SIZ),NULL,NULL,SECURE_BOOT)))
		{
			while(1);
		}

		if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_MOBILEVISOR_ADDR - KEY_INFO_SIZ),NULL,NULL,SECURE_BOOT)))
		{
			while(1);
		}
	}
#endif
    if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_SYS_NAND_U_BOOT_DST - SECURE_HEADER_OFF),NULL,NULL,SECURE_BOOT)))
    {
      /***secboot uboot: fall back to the other A/B slot***/
      load_partition_with_header(spl_slot_name("uboot", spl_other_suffix()),CONFIG_SYS_EMMC_U_BOOT_SECTOR_NUM*EMMC_SECTOR_SIZE,CONFIG_SYS_NAND_U_BOOT_DST,(sys_img_header*)(CONFIG_SYS_NAND_U_BOOT_DST - EMMC_SECTOR_SIZE));
      if(SECBOOT_VERIFY_SUCCESS != (secboot_verify(IRAM_BEGIN,(CONFIG_SYS_NAND_U_BOOT_DST - SECURE_HEADER_OFF),NULL,NULL,SECURE_BOOT)))
        {
         while(1);
        }
    }

if (!sysdump_mode)
	update_swVersion();

#endif
#ifdef CONFIG_SPL_VIBRATE_MARKERS
	spl_buzz(5);   /* STAGE 5: all boot images loaded + verified from eMMC */
#endif
	/*
	 * Jump to U-Boot image
	 */
#ifdef SPRD_EVM_TAG_ON
	SPRD_EVM_TAG(3);
#endif

	/* If we didn't load any secure world and we got this far, we need
	 * to jump directly to U-Boot rather than an empty destination. */
	if (!load_secure) {
#ifdef CONFIG_SPL_VIBRATE_MARKERS
		spl_buzz(7);   /* STAGE 6 + 1: DDR + load done, jumping to just U-Boot. */
#endif
		((void (*)(void))(void *)CONFIG_SYS_NAND_U_BOOT_START)();
		while (1);
	}

#if CONFIG_SMLBOOT || CONFIG_LOAD_ATF
	if (!sysdump_security) {
		uboot = (void *)CONFIG_SML_LDADDR_START;
	} else {
		uboot = (void *)CONFIG_SYS_NAND_U_BOOT_START;
	}
#else
#ifdef CONFIG_MOBILEVISOR
	if (sysdump_mode) {
		uboot = (void *)CONFIG_SYS_NAND_U_BOOT_START;
	} else {
		uboot = (void *)CONFIG_MOBILEVISOR_ADDR;
	}
#else
	uboot = (void *)CONFIG_SYS_NAND_U_BOOT_START;
#endif
#endif

#ifdef CONFIG_SECURE_BOOT
#if defined(CONFIG_SC8830) || defined(CONFIG_SC9630)  || defined(CONFIG_SCX35L64)
	typedef sec_callback_func_t *(*get_secure_checkfunc_t) (sec_callback_func_t *);
	sec_callback_func_t sec_callfunc;
	vlr_info_t *vlr_info = (vlr_info_t *) (VLR_INFO_OFF);
	get_secure_checkfunc_t *get_secure_func = (get_secure_checkfunc_t *) (INTER_RAM_BEGIN + 0x1C);
	(*get_secure_func) (&sec_callfunc);
	sec_callfunc.secure_check((CONFIG_SYS_NAND_U_BOOT_START - KEY_INFO_SIZ), vlr_info->length, vlr_info, KEY_INFO_OFF);
	//sec_callfunc.secure_check((CONFIG_SYS_NAND_U_BOOT_START),vlr_info->length, vlr_info, KEY_INFO_OFF);
#elif defined(CONFIG_FPGA)

#else
	secure_check(CONFIG_SYS_NAND_U_BOOT_START, 0, CONFIG_SYS_NAND_U_BOOT_START + CONFIG_SYS_NAND_U_BOOT_SIZE - VLR_INFO_OFF,
		     INTER_RAM_BEGIN + CONFIG_SPL_LOAD_LEN - KEY_INFO_SIZ - CUSTOM_DATA_SIZ);
#endif
#endif

    /* disable ahb ccp_en bit */
#if defined(CONFIG_EFUSE)
#if defined(CONFIG_WHALE) || defined(CONFIG_WHALE2)
    sync_efuse_ahb_ccp_eb();
#endif
#endif

#if defined(CONFIG_WHALE) || defined(CONFIG_WHALE2)
	disable_ahb_cc63_eb();
#endif
	disable_ahb_ce_eb();

	disable_ahb_ce_efs_eb();

#ifdef CONFIG_EMMC_BOOT
	/* disable emmc sd_clk */
	Emmc_DisSdClk();
#endif
	/***firewall set***/
	if (!sysdump_security) {  //sysdump mode (tos panic) don't enable firewall
		sprd_firewall_config();
	}

	/* Stock parity: select the crypto-engine (CE) 2X clock source
	 * (REG_AP_CLK_CORE_CGM_CE_CFG @ 0x20200068, bits[1:0]=3) before handing off to
	 * the secure world (SML/trustos), which uses the CE for crypto. Stock does this
	 * write right before the SML jump; our older source omitted it. */
	if ((REG32(0x20200068) & 0x3) != 0x3)
		REG32(0x20200068) |= 0x3;

#ifdef CONFIG_SPL_EMMC_TRACE
	/* persist verbose pre-jump HW state to uboot_log for cold-vs-warm diffing */
	spl_emmc_trace_snapshot();
#endif

#if defined(CONFIG_SCX35L64)
#ifndef CONFIG_X86
	chipram_env_set(BOOTLOADER_MODE_LOAD);
	extern void switch64_and_set_pc(u32 addr);
	switch64_and_set_pc(CONFIG_SYS_NAND_U_BOOT_START);
#endif
#if  defined(CONFIG_CHIP_ENV_SET)
	chipram_env_set(BOOTLOADER_MODE_LOAD);
#endif
	(*uboot) ();
#else
#if  defined(CONFIG_CHIP_ENV_SET)
	chipram_env_set(BOOTLOADER_MODE_LOAD);
#endif

#ifdef CONFIG_SPL_VIBRATE_MARKERS
	spl_buzz(6);   /* STAGE 6: DDR + images + firewall done, jumping to SML/secure world */
#endif
#ifdef CONFIG_SPL_DIAG_SKIP_SML
	/* Diagnostic bisection: after the full, identical pre-jump setup, jump
	 * straight to u-boot (0x9f000000) instead of SML. If the panel/logo comes up
	 * on the failing (post-Android-shutdown) boot, the hang is in the SML/secure
	 * handoff (RPMB / secure storage / secure-DDR provisioned by Android). If it
	 * stays black, the hang is in DDR or code execution from DRAM, not SML. */
	((void (*)(void))(void *)CONFIG_SYS_NAND_U_BOOT_START)();
	while (1);
#endif
#if (CONFIG_LOAD_TOS_ALONE == 1) && !defined (CONFIG_ATF_BOOT_TOS)
	secure_sp_entry(CONFIG_TOS_LDADDR_START,CONFIG_SYS_NAND_U_BOOT_START);
#else
#if defined(CONFIG_LOAD_ATF) && defined(CONFIG_TEECFG_LDADDR_START)
	/* Enter SML/ATF with x0 = trustos base, x1 = teecfg base, matching stock. */
	((void (*)(unsigned long, unsigned long))uboot)(CONFIG_TOS_LDADDR_START, CONFIG_TEECFG_LDADDR_START);
#else
	(*uboot) ();
#endif
#endif
#endif
}
