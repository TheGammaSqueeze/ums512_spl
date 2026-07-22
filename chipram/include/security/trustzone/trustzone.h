#ifndef _TRUSTZONE_H
#define _TRUSTZONE_H

/***************************************************************
 * data structure
 **************************************************************/
typedef struct {
	uint32_t tos_size;
} sprd_fw_attr;

void secure_sp_entry(unsigned long s_entry,unsigned long ns_entry);

void sprd_firewall_config(void);

void sprd_firewall_config_pre (void);

/*
 * sprd_firewall_usb_clk_enable
 * description: enable the OTG UTMI clock only, with no secure-world locking.
 * Split out of sprd_firewall_config_pre() so a non-secure (mainline) boot can
 * keep the always-on USB clk without applying the CONFIG_SECBOOT firewall.
 */
void sprd_firewall_usb_clk_enable (void);

/*
 * sprd_firewall_config_attr
 * description:config firewall attribute of specific modules.
 */
void sprd_firewall_config_attr (sprd_fw_attr *attr);
#endif
