#include <part.h>
#include <common.h>
extern block_dev_desc_t sprd_mmc_dev;
extern block_dev_desc_t sprd_ufs_dev;


/* Active boot block device. NULL means "use the compile-time default/fallback". */
static block_dev_desc_t *g_boot_dev = NULL;

void spl_set_boot_dev(block_dev_desc_t *dev)
{
	g_boot_dev = dev;
}

block_dev_desc_t *get_dev()
{
	if (g_boot_dev)
		return g_boot_dev;
#ifdef CONFIG_EMMC_BOOT
	return mmc_get_dev();
#elif defined(CONFIG_UFS_BOOT)
	return ufs_get_dev();
#endif
}

int get_partition_info_by_name (block_dev_desc_t *dev_desc, uchar * partition_name,
						disk_partition_t *info)
{
	switch(dev_desc->part_type){
#ifdef CONFIG_EFI_PARTITION
	case PART_TYPE_EFI:
		if (get_partition_info_by_name_efi(dev_desc, partition_name, info)== 0) {
			//PRINTF ("## Valid EFI partition found ##\n");
			return (0);
		}
		break;
#endif
	default:
		break;
	}
	return (-1);
}
