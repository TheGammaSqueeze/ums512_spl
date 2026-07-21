/*
* Copyright (c) 2018, Spreadtrum Communications.
*
* The above copyright notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <linux/types.h>
#include <common.h>
#include <asm/arch/sprd_reg.h>
#include <security/sec_string.h>
#include <security/trustzone/trustzone.h>

#define REG32(_x_)	(*(volatile uint32_t*)(_x_))

#define SET_REG_OFF			0x1000
#define CLR_REG_OFF			0x2000

#define SPRD_MST_CTRL_AON_BASE		(SPRD_SEC_TZPC_PHYS + 0x3000)
#define SPRD_MST_CTRL_AP_BASE		(SPRD_SEC_TZPC_PHYS + 0x8000)
#define SPRD_SLV_FW_AON0_BASE		(SPRD_SEC_TZPC_PHYS + 0x0000)
#define SPRD_SLV_FW_AON1_BASE		(SPRD_SEC_TZPC_PHYS + 0xE000)
#define SPRD_SLV_FW_AP0_BASE		(SPRD_SEC_TZPC_PHYS + 0x7000)
#define SPRD_REG_FW_AON_APB_BASE	(SPRD_SEC_TZPC_PHYS + 0x4000)
#define SPRD_REG_FW_AON_PMU_BASE	(SPRD_SEC_TZPC_PHYS + 0x5000)
#define SPRD_REG_FW_AON_PIN_BASE	(SPRD_SEC_TZPC_PHYS + 0x6000)
#define SPRD_REG_FW_AP_AHB_BASE		(SPRD_SEC_TZPC_PHYS + 0xA000)
#define SPRD_REG_FW_AP_APB_BASE		(SPRD_SEC_TZPC_PHYS + 0x9000)
#define SPRD_MEM_FW_AON_BASE		(SPRD_SEC_TZPC_PHYS + 0x1000)
#define SPRD_MEM_FW_PUB_BASE		(SPRD_SEC_TZPC_PHYS + 0xB000)

#define MST_ID_MAX_NUM			(128)
#define MST_ID_ARRAY_LEN		(MST_ID_MAX_NUM/32)

#define MST_ID_DAP			0x00
#define MST_ID_CPU			0x01
#define MST_ID_CM4			0x73

#define MST_CTRL_AON_RD0_OFF		(0x0000)
#define MST_CTRL_AON_WR0_OFF		(0x0004)

#define MST_CTRL_AP_RD0_OFF		(0x0000)
#define MST_CTRL_AP_WR0_OFF		(0x0004)

#define SLV_FW_AON0_RD5_OFF		(0x004C)
#define SLV_FW_AON0_WR5_OFF		(0x0064)

#define SLV_FW_AON1_RD2_OFF		(0x0020)
#define SLV_FW_AON1_WR2_OFF		(0x0030)

#define SLV_FW_AP0_RD0_OFF		(0x0028)
#define SLV_FW_AP0_WR0_OFF		(0x0038)

#define REG_FW_AON_APB_RD0		(0x0000)
#define REG_FW_AON_APB_WR0		(0x0018)

#define REG_FW_AON_APB_CTRL_ADDR0_OFF	(0x0030)
#define REG_FW_AON_APB_CTRL_VAL0_OFF	(0x0070)

#define REG_FW_AON_PMU_CTRL_ADDR0_OFF	(0x0058)
#define REG_FW_AON_PMU_CTRL_ADDR1_OFF	(0x005C)
#define REG_FW_AON_PMU_CTRL_ADDR2_OFF	(0x0060)
#define REG_FW_AON_PMU_CTRL_VAL0_OFF	(0x0098)
#define REG_FW_AON_PMU_CTRL_VAL1_OFF	(0x009C)
#define REG_FW_AON_PMU_CTRL_VAL2_OFF	(0x00A0)

#define REG_FW_AP_APB_CTRL_ADDR0_OFF	(0x0008)
#define REG_FW_AP_APB_CTRL_ADDR1_OFF	(0x000C)
#define REG_FW_AP_APB_CTRL_VAL0_OFF	(0x0048)
#define REG_FW_AP_APB_CTRL_VAL1_OFF	(0x004C)

#define MEM_FW_SEG_OFF			(0x1000)
#define MEM_FW_SEG_LEN			(0x80)

#define AON_ADDR_SHIFT_BITS		(4)
#define PUB_ADDR_SHIFT_BITS		(12)

#define USB_RD_SEC			BIT(21)
#define USB_WR_SEC			BIT(21)

//master ctrl ap
#define SDIO0_RD_SEC			(BIT(21)|BIT(20))
#define SDIO0_WR_SEC			(BIT(21)|BIT(20))

#define EMMC_RD_SEC			(BIT(15)|BIT(14))
#define EMMC_WR_SEC			(BIT(15)|BIT(14))

//slave fw aon0
#define PUB_CTRL_ATTR_MASK		(BIT(9)|BIT(8))
#define PUB_CTRL_ATTR_RD		(BIT(9)|BIT(8))		//security/non_security accesss
#define PUB_CTRL_ATTR_WR		BIT(8)			//security access only

//slave fw aon1
#define KEYPAD_CTRL_ATTR_MASK		(BIT(9)|BIT(8))
#define KEYPAD_CTRL_ATTR_RD		BIT(8)			//security access only
#define KEYPAD_CTRL_ATTR_WR		BIT(8)			//security access only

//slave fw ap0
#define SCE_SLV_ATTR_MASK		(BIT(5)|BIT(4))
#define SCE_SLV_ATTR_RD			BIT(4)			//security access only
#define SCE_SLV_ATTR_WR			BIT(4)			//security access only

// IRAM for PM
#define IRAM_PM_ADDR	0x00000000
#ifdef CONFIG_SPL_FW_PARITY
#define IRAM_PM_SIZE	0x800		/* stock: PM iram fw last_addr = 0x7f */
#else
#define IRAM_PM_SIZE	0x2000
#endif

// IRAM for FW
#define IRAM_FW_ADDR	0x00002000
#define IRAM_FW_SIZE	0x1000

// IRAM for efuse
#ifdef CONFIG_SPL_FW_PARITY
#define IRAM_EFUSE_ADDR	0x00000800	/* stock: efuse iram fw first/last = 0x80/0xbf */
#else
#define IRAM_EFUSE_ADDR	0x00015C00
#endif
#define IRAM_EFUSE_SIZE	0x00000400

typedef struct {
	volatile uint32_t first_addr;
	volatile uint32_t last_addr;
	volatile uint32_t mst_id_rd_sec[MST_ID_ARRAY_LEN];
	volatile uint32_t mst_id_rd_nsec[MST_ID_ARRAY_LEN];
	volatile uint32_t mst_id_wr_sec[MST_ID_ARRAY_LEN];
	volatile uint32_t mst_id_wr_nsec[MST_ID_ARRAY_LEN];
} sprd_mem_seg_cfg;

typedef struct {
	volatile uint32_t first_addr;
	volatile uint32_t last_addr;
	volatile uint32_t mst_id_sec[MST_ID_ARRAY_LEN];
	volatile uint32_t mst_id_nsec[MST_ID_ARRAY_LEN];
} sprd_slv_seg_cfg;

static void sdio_unsec(void)
{
	/* make sdio non-secure */
	REG32(SPRD_MST_CTRL_AP_BASE + MST_CTRL_AP_RD0_OFF) &= ~SDIO0_RD_SEC;
	REG32(SPRD_MST_CTRL_AP_BASE + MST_CTRL_AP_WR0_OFF) &= ~SDIO0_WR_SEC;
}

static void emmc_unsec(void)
{
	/* make emmc non-secure */
	REG32(SPRD_MST_CTRL_AP_BASE + MST_CTRL_AP_RD0_OFF) &= ~EMMC_RD_SEC;
	REG32(SPRD_MST_CTRL_AP_BASE + MST_CTRL_AP_WR0_OFF) &= ~EMMC_WR_SEC;
}

static void usb_unsec(void)
{
	/* make usb non-secure */
	REG32(SPRD_MST_CTRL_AON_BASE + MST_CTRL_AON_RD0_OFF) &= ~USB_RD_SEC;
	REG32(SPRD_MST_CTRL_AON_BASE + MST_CTRL_AON_WR0_OFF) &= ~USB_WR_SEC;
}

static void sce_sec(void)
{
	uint32_t val;

	/* set master attr */
	// master is secure. No config is needed

	/* set slave attr */
	// non-sec access is forbidden by itself. No config is needed
	val = REG32(SPRD_SLV_FW_AP0_BASE + SLV_FW_AP0_RD0_OFF);
	val &= ~SCE_SLV_ATTR_MASK;
	val |= SCE_SLV_ATTR_RD;
	REG32(SPRD_SLV_FW_AP0_BASE + SLV_FW_AP0_RD0_OFF) = val;

	val = REG32(SPRD_SLV_FW_AP0_BASE + SLV_FW_AP0_WR0_OFF);
	val &= ~SCE_SLV_ATTR_MASK;
	val |= SCE_SLV_ATTR_WR;
	REG32(SPRD_SLV_FW_AP0_BASE + SLV_FW_AP0_WR0_OFF) = val;

	/* protect enable bit */
	REG32(SPRD_REG_FW_AP_APB_BASE + REG_FW_AP_APB_CTRL_ADDR0_OFF) = REG_AP_APB_APB_EB;
	REG32(SPRD_REG_FW_AP_APB_BASE + REG_FW_AP_APB_CTRL_VAL0_OFF)  = BIT_AP_APB_CE_SEC_EB;

	/* protect softreset bit */
	REG32(SPRD_REG_FW_AP_APB_BASE + REG_FW_AP_APB_CTRL_ADDR1_OFF) = REG_AP_APB_APB_RST;
	REG32(SPRD_REG_FW_AP_APB_BASE + REG_FW_AP_APB_CTRL_VAL1_OFF)  = BIT_AP_APB_CE_SEC_SOFT_RST;
}

static void cm4_sec(void)
{
	uint32_t val;

	/* master is secure. No config is needed */

	/* cm4 is always enabled */

	/* protect soft-reset bit */
	val = REG32(SPRD_REG_FW_AON_APB_BASE + REG_FW_AON_APB_WR0);
	val |= BIT_REG_FW0_AON_CM4_SYS_SOFT_RST_WR_SEC;
	REG32(SPRD_REG_FW_AON_APB_BASE + REG_FW_AON_APB_WR0) = val;
}

static void cp_sec (void)
{
	/* protect reset/sleep/shutdown bits */

	//PUBCP_CR5_CORE_SOFT_RST
	REG32(SPRD_REG_FW_AON_APB_BASE + REG_FW_AON_APB_CTRL_ADDR0_OFF) = REG_AON_APB_PCP_SOFT_RST;
	REG32(SPRD_REG_FW_AON_APB_BASE + REG_FW_AON_APB_CTRL_VAL0_OFF) = BIT_AON_APB_PUBCP_CR5_CORE_SOFT_RST;

	//PD_PUBCP_SYS_FORCE_SHUTDONW
	REG32(SPRD_REG_FW_AON_PMU_BASE + REG_FW_AON_PMU_CTRL_ADDR0_OFF) = REG_PMU_APB_PD_PUBCP_SYS_CFG;
	REG32(SPRD_REG_FW_AON_PMU_BASE + REG_FW_AON_PMU_CTRL_VAL0_OFF) = BIT_PMU_APB_PD_PUBCP_SYS_FORCE_SHUTDOWN;

	//PUBCP_SOFT_RST
	REG32(SPRD_REG_FW_AON_PMU_BASE + REG_FW_AON_PMU_CTRL_ADDR1_OFF) = REG_PMU_APB_CP_SOFT_RST;
	REG32(SPRD_REG_FW_AON_PMU_BASE + REG_FW_AON_PMU_CTRL_VAL1_OFF) = BIT_PMU_APB_PUBCP_SOFT_RST;

	//PUBCP_FORCE_DEEP_SLEEP
	REG32(SPRD_REG_FW_AON_PMU_BASE + REG_FW_AON_PMU_CTRL_ADDR2_OFF) = REG_PMU_APB_SLEEP_CTRL;
	REG32(SPRD_REG_FW_AON_PMU_BASE + REG_FW_AON_PMU_CTRL_VAL2_OFF) = BIT_PMU_APB_PUBCP_FORCE_DEEP_SLEEP;
}

static void iram_sec (void)
{
	uint32_t i;
	sprd_mem_seg_cfg *mem_seg_addr;

	/* iram 0x0000 ~ 0x1FFF secure read/write. Used by SML */
	mem_seg_addr = (sprd_mem_seg_cfg *)((uint64_t)(SPRD_MEM_FW_AON_BASE + MEM_FW_SEG_OFF));
	REG32(&(mem_seg_addr->first_addr)) = 0xFFFFFFFF;
	for (i = 0; i < MST_ID_ARRAY_LEN; i++) {
		REG32(&(mem_seg_addr->mst_id_rd_sec[i]))  = 0x0;
		REG32(&(mem_seg_addr->mst_id_rd_nsec[i])) = 0x0;
		REG32(&(mem_seg_addr->mst_id_wr_sec[i]))  = 0x0;
		REG32(&(mem_seg_addr->mst_id_wr_nsec[i])) = 0x0;
	}
	// only DAP/CPU/CM4 can access
	REG32(&(mem_seg_addr->mst_id_rd_sec[0]))  = BIT(MST_ID_DAP) | BIT(MST_ID_CPU);
	REG32(&(mem_seg_addr->mst_id_wr_sec[0]))  = BIT(MST_ID_DAP) | BIT(MST_ID_CPU);
	REG32(&(mem_seg_addr->mst_id_rd_sec[3]))  = BIT(MST_ID_CM4 % 32);
	REG32(&(mem_seg_addr->mst_id_wr_sec[3]))  = BIT(MST_ID_CM4 % 32);
	REG32(&(mem_seg_addr->last_addr)) = ((IRAM_PM_ADDR + IRAM_PM_SIZE - 1) >> AON_ADDR_SHIFT_BITS);
	REG32(&(mem_seg_addr->first_addr)) = (IRAM_PM_ADDR >> AON_ADDR_SHIFT_BITS);

	/* iram 0x2000 ~ 0x2FFF secure read/write. Used by TOS */
        mem_seg_addr = (sprd_mem_seg_cfg *)((uint64_t)(SPRD_MEM_FW_AON_BASE + MEM_FW_SEG_OFF + MEM_FW_SEG_LEN));
        REG32(&(mem_seg_addr->first_addr)) = 0xFFFFFFFF;
        for (i = 0; i < MST_ID_ARRAY_LEN; i++) {
        	REG32(&(mem_seg_addr->mst_id_rd_sec[i]))  = 0x0;
        	REG32(&(mem_seg_addr->mst_id_rd_nsec[i])) = 0x0;
        	REG32(&(mem_seg_addr->mst_id_wr_sec[i]))  = 0x0;
        	REG32(&(mem_seg_addr->mst_id_wr_nsec[i])) = 0x0;
        }
        // only DAP/CPU/CM4 can access
	REG32(&(mem_seg_addr->mst_id_rd_sec[0]))  = BIT(MST_ID_DAP) | BIT(MST_ID_CPU);
	REG32(&(mem_seg_addr->mst_id_wr_sec[0]))  = BIT(MST_ID_DAP) | BIT(MST_ID_CPU);
	REG32(&(mem_seg_addr->mst_id_rd_sec[3]))  = BIT(MST_ID_CM4 % 32);
	REG32(&(mem_seg_addr->mst_id_wr_sec[3]))  = BIT(MST_ID_CM4 % 32);
        REG32(&(mem_seg_addr->last_addr)) = ((IRAM_FW_ADDR + IRAM_FW_SIZE - 1) >> AON_ADDR_SHIFT_BITS);
        REG32(&(mem_seg_addr->first_addr)) = (IRAM_FW_ADDR >> AON_ADDR_SHIFT_BITS);

        /* iram 0x15C00 ~ 0x15FFF read only */
	mem_seg_addr = (sprd_mem_seg_cfg *)((uint64_t)(SPRD_MEM_FW_AON_BASE + MEM_FW_SEG_OFF + MEM_FW_SEG_LEN * 2));
	REG32(&(mem_seg_addr->first_addr)) = 0xFFFFFFFF;
	for (i = 0; i < MST_ID_ARRAY_LEN; i++) {
		REG32(&(mem_seg_addr->mst_id_rd_sec[i]))  = 0xFFFFFFFF;
		REG32(&(mem_seg_addr->mst_id_rd_nsec[i])) = 0xFFFFFFFF;
		REG32(&(mem_seg_addr->mst_id_wr_sec[i]))  = 0x0;
		REG32(&(mem_seg_addr->mst_id_wr_nsec[i])) = 0x0;
	}
	REG32(&(mem_seg_addr->last_addr)) = ((IRAM_EFUSE_ADDR + IRAM_EFUSE_SIZE - 1) >> AON_ADDR_SHIFT_BITS);
	REG32(&(mem_seg_addr->first_addr)) = (IRAM_EFUSE_ADDR >> AON_ADDR_SHIFT_BITS);
}

static void dmc_sec (void)
{
	uint32_t val;

	// protect DDR controller
	val = REG32(SPRD_SLV_FW_AON0_BASE + SLV_FW_AON0_WR5_OFF);
	val &= ~PUB_CTRL_ATTR_MASK;
	val |= PUB_CTRL_ATTR_WR;
	REG32(SPRD_SLV_FW_AON0_BASE + SLV_FW_AON0_WR5_OFF) = val;

	/* enable mem_fw_pub, */
	/* rf_fw_en is tied 1, always enabled */
}

static void sml_teecfg_sec (void)
{
	uint32_t i;
	sprd_mem_seg_cfg *mem_seg_addr;

	mem_seg_addr = (sprd_mem_seg_cfg *)((uint64_t)(SPRD_MEM_FW_PUB_BASE + MEM_FW_SEG_OFF));
	REG32(&(mem_seg_addr->first_addr)) = 0xFFFFFFFF;
	for (i = 0; i < MST_ID_ARRAY_LEN; i++) {
		REG32(&(mem_seg_addr->mst_id_rd_sec[i]))  = 0xFFFFFFFF;
		REG32(&(mem_seg_addr->mst_id_rd_nsec[i])) = 0x0;
		REG32(&(mem_seg_addr->mst_id_wr_sec[i]))  = 0xFFFFFFFF;
		REG32(&(mem_seg_addr->mst_id_wr_nsec[i])) = 0x0;
	}
#ifdef CONFIG_SPL_FW_PARITY
	/* stock parity: seg0 initially covers TEECFG only (0x94040000..0x9405ffff,
	 * first=0x14040 last=0x1405f); tos_sec() extends last_addr to the end of TOS
	 * once teecfg is loaded. Stock does NOT put SML or the 32MB tail in seg0. */
	REG32(&(mem_seg_addr->last_addr)) = (CONFIG_TEECFG_LDADDR_START + 0x20000 - 1 - 0x80000000)>>PUB_ADDR_SHIFT_BITS;
	REG32(&(mem_seg_addr->first_addr)) = (CONFIG_TEECFG_LDADDR_START - 0x80000000)>>PUB_ADDR_SHIFT_BITS;
#else
	REG32(&(mem_seg_addr->last_addr)) = (CONFIG_SML_LDADDR_START + CONFIG_SEC_MEM_SIZE - 1 - 0x80000000)>>PUB_ADDR_SHIFT_BITS;
	REG32(&(mem_seg_addr->first_addr)) = (CONFIG_SML_LDADDR_START - 0x80000000)>>PUB_ADDR_SHIFT_BITS;
#endif
}

static void tos_sec (uint32_t tos_size)
{
	sprd_mem_seg_cfg *mem_seg_addr;

	mem_seg_addr = (sprd_mem_seg_cfg *)((uint64_t)(SPRD_MEM_FW_PUB_BASE + MEM_FW_SEG_OFF));
#ifdef CONFIG_SPL_FW_PARITY
	/* stock parity: extend seg0 last_addr to the end of TOS (TEECFG..TOS end),
	 * = (TOS_LDADDR + tos_size)>>12; for a 6MB tos this is 0x1465f like stock. */
	REG32(&(mem_seg_addr->last_addr)) = (CONFIG_TOS_LDADDR_START + tos_size - 1 - 0x80000000)>>PUB_ADDR_SHIFT_BITS;
#else
	REG32(&(mem_seg_addr->last_addr)) = (CONFIG_SML_LDADDR_START + CONFIG_SEC_MEM_SIZE + tos_size - 1 - 0x80000000)>>PUB_ADDR_SHIFT_BITS;
#endif
}

/* PUB memory-firewall segment 7 (0x3280C380): the "above top-of-DRAM" catch-all.
 * Stock reprograms this every boot (nand_boot -> firewall_config_pre) to deny ALL
 * bus masters (secure AND non-secure) the region from top-of-populated-DRAM up to
 * 4GB, with the boundary derived from the detected DRAM size in CHIPRAM_ENV. The
 * TZPC/mem-firewall block is always-on and NOT reset by a warm reset, so stock
 * relies on rewriting it every boot. Our SPL never programmed it, so once Android's
 * secure world (TOS) has touched this segment, our next boot left it stale - which
 * only bites after Android has provisioned, matching the observed failure. */
#define CHIPRAM_ENV_DRAM_SIZE_ADDR	(0x82000000 + 0x8)   /* u64 dram_size lo word */
static void mem_top_catchall_sec (void)
{
	uint32_t i;
	uint32_t dram_size;
	sprd_mem_seg_cfg *seg_addr;

	/* total DRAM bytes (cs0+cs1) that dmc_update_param_for_uboot wrote to
	 * CHIPRAM_ENV during DDR init. Matches the value stock's catch-all uses. */
	dram_size = REG32(CHIPRAM_ENV_DRAM_SIZE_ADDR);

	/* Safety: a first_addr below the real top-of-DRAM would deny DRAM to every
	 * master and hang instantly. The env dram_size on this device is a stable
	 * 0xC0000000 (3GB); only program the catch-all when the env reports a
	 * plausible size (>= 2GB), otherwise leave the segment untouched. */
	if (dram_size < 0x80000000)
		return;

	seg_addr = (sprd_mem_seg_cfg *)((uint64_t)(SPRD_MEM_FW_PUB_BASE + MEM_FW_SEG_OFF + 7 * MEM_FW_SEG_LEN));
	REG32(&(seg_addr->first_addr)) = 0xFFFFFFFF;   /* disable while reconfiguring */
	for (i = 0; i < MST_ID_ARRAY_LEN; i++) {
		REG32(&(seg_addr->mst_id_rd_sec[i]))  = 0x0;   /* deny every master */
		REG32(&(seg_addr->mst_id_rd_nsec[i])) = 0x0;
		REG32(&(seg_addr->mst_id_wr_sec[i]))  = 0x0;
		REG32(&(seg_addr->mst_id_wr_nsec[i])) = 0x0;
	}
	REG32(&(seg_addr->last_addr))  = 0xFFFFFFFF;
	REG32(&(seg_addr->first_addr)) = dram_size >> PUB_ADDR_SHIFT_BITS;
}

// special for sharkl5. Only setting slv_fw_aon1
static void slv_fw_aon1_special (void) {
	uint32_t val;

	// keypad
	val = REG32(SPRD_SLV_FW_AON1_BASE + SLV_FW_AON1_RD2_OFF);
	val &= ~KEYPAD_CTRL_ATTR_MASK;
	val |= KEYPAD_CTRL_ATTR_RD;
	REG32(SPRD_SLV_FW_AON1_BASE + SLV_FW_AON1_RD2_OFF) = val;

	val = REG32(SPRD_SLV_FW_AON1_BASE + SLV_FW_AON1_WR2_OFF);
	val &= ~KEYPAD_CTRL_ATTR_MASK;
	val |= KEYPAD_CTRL_ATTR_WR;
	REG32(SPRD_SLV_FW_AON1_BASE + SLV_FW_AON1_WR2_OFF) = val;
}

static void disable_tzpc (void)
{
	/* disable tzpc */
	REG32(REG_AON_SEC_APB_SEC_EB + CLR_REG_OFF) = BIT_AON_SEC_APB_SEC_TZPC_EB;
}

void sprd_firewall_config_pre (void)
{
	// special for sharkl5, enable USB clk.
	REG32(REG_AON_APB_APB_EB1 + SET_REG_OFF) = BIT_AON_APB_OTG_UTMI_EB;

#ifdef CONFIG_SECBOOT
	sce_sec();
	cm4_sec();
	cp_sec();

	dmc_sec();
	sml_teecfg_sec();
#ifdef CONFIG_SPL_FW_SEG7
	/* stock-parity: reprogram PUB seg7 above-DRAM catch-all every boot */
	mem_top_catchall_sec();
#endif

	slv_fw_aon1_special();
#endif
}

void sprd_firewall_config_attr (sprd_fw_attr *attr)
{
#ifdef CONFIG_ATF_BOOT_TOS
	tos_sec(attr->tos_size);
#endif
}

void sprd_firewall_config (void)
{
	sdio_unsec();
	emmc_unsec();
	usb_unsec();

	iram_sec();

	//disable_tzpc();

	// special for sharkl5, enable USB clk.
	REG32(REG_AON_APB_APB_EB1 + CLR_REG_OFF) = BIT_AON_APB_OTG_UTMI_EB;
}
