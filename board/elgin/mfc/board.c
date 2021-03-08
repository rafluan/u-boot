// SPDX-License-Identifier: GPL-2.0+
/*
 * board.c
 *
 * Board functions for TI AM335X based boards
 *
 * Copyright (C) 2011, Texas Instruments, Incorporated - http://www.ti.com/
 */

#include <common.h>
#include <dm.h>
#include <env.h>
#include <errno.h>
#include <image.h>
#include <init.h>
#include <malloc.h>
#include <net.h>
#include <spl.h>
#include <serial.h>
#include <asm/arch/cpu.h>
#include <asm/arch/hardware.h>
#include <asm/arch/omap.h>
#include <asm/arch/ddr_defs.h>
#include <asm/arch/clock.h>
#include <asm/arch/clk_synthesizer.h>
#include <asm/arch/gpio.h>
#include <asm/arch/mmc_host_def.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch/mem.h>
#include <asm/io.h>
#include <asm/emif.h>
#include <asm/gpio.h>
#include <asm/omap_common.h>
#include <asm/omap_sec_common.h>
#include <asm/omap_mmc.h>
#include <i2c.h>
#include <miiphy.h>
#include <cpsw.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <power/tps65217.h>
#include <power/tps65910.h>
#include <env_internal.h>
#include <watchdog.h>
#include "board.h"

DECLARE_GLOBAL_DATA_PTR;

#ifndef CONFIG_SKIP_LOWLEVEL_INIT
static const struct ddr_data ddr2_data = {
	.datardsratio0 = MT47H128M16RT25E_RD_DQS,
	.datafwsratio0 = MT47H128M16RT25E_PHY_FIFO_WE,
	.datawrsratio0 = MT47H128M16RT25E_PHY_WR_DATA,
};

static const struct cmd_control ddr2_cmd_ctrl_data = {
	.cmd0csratio = MT47H128M16RT25E_RATIO,
	.cmd1csratio = MT47H128M16RT25E_RATIO,
	.cmd2csratio = MT47H128M16RT25E_RATIO,
};

static const struct emif_regs ddr2_emif_reg_data = {
	.sdram_config = MT47H128M16RT25E_EMIF_SDCFG,
	.ref_ctrl = MT47H128M16RT25E_EMIF_SDREF,
	.sdram_tim1 = MT47H128M16RT25E_EMIF_TIM1,
	.sdram_tim2 = MT47H128M16RT25E_EMIF_TIM2,
	.sdram_tim3 = MT47H128M16RT25E_EMIF_TIM3,
	.emif_ddr_phy_ctlr_1 = MT47H128M16RT25E_EMIF_READ_LATENCY,
};

const struct dpll_params *get_dpll_ddr_params(void)
{
	int ind = get_sys_clk_index();
	return &dpll_ddr2_266MHz[ind];
}

const struct dpll_params *get_dpll_mpu_params(void)
{
	struct ctrl_dev *cdev = (struct ctrl_dev *)CTRL_DEVICE_BASE;
	int ind = get_sys_clk_index();
	int freq = am335x_get_efuse_mpu_max_freq(cdev);

	switch (freq) {
	case MPUPLL_M_1000:
		return &dpll_mpu_opp[ind][5];
	case MPUPLL_M_800:
		return &dpll_mpu_opp[ind][4];
	case MPUPLL_M_720:
		return &dpll_mpu_opp[ind][3];
	case MPUPLL_M_600:
		return &dpll_mpu_opp[ind][2];
	case MPUPLL_M_500:
		return &dpll_mpu_opp100;
	case MPUPLL_M_300:
		return &dpll_mpu_opp[ind][0];
	}

	return &dpll_mpu_opp[ind][0];
}

static void scale_vcores_tps65217(int freq)
{
	int usb_cur_lim, mpu_vdd;

	if (power_tps65217_init(0))
		return;

	switch (freq) {
	case MPUPLL_M_1000:
		mpu_vdd = TPS65217_DCDC_VOLT_SEL_1325MV;
		usb_cur_lim = TPS65217_USB_INPUT_CUR_LIMIT_1800MA;
		break;
	case MPUPLL_M_800:
		mpu_vdd = TPS65217_DCDC_VOLT_SEL_1275MV;
		usb_cur_lim = TPS65217_USB_INPUT_CUR_LIMIT_1300MA;
		break;
	case MPUPLL_M_720:
		mpu_vdd = TPS65217_DCDC_VOLT_SEL_1200MV;
		usb_cur_lim = TPS65217_USB_INPUT_CUR_LIMIT_1300MA;
		break;
	case MPUPLL_M_600:
	case MPUPLL_M_500:
	case MPUPLL_M_300:
	default:
		mpu_vdd = TPS65217_DCDC_VOLT_SEL_1100MV;
		usb_cur_lim = TPS65217_USB_INPUT_CUR_LIMIT_1300MA;
		break;
	}

	if (tps65217_reg_write(TPS65217_PROT_LEVEL_NONE,
			       TPS65217_POWER_PATH,
			       usb_cur_lim,
			       TPS65217_USB_INPUT_CUR_LIMIT_MASK))
		puts("tps65217_reg_write failure\n");

	/* Set DCDC3 (CORE) voltage to 1.10V */
	if (tps65217_voltage_update(TPS65217_DEFDCDC3,
				    TPS65217_DCDC_VOLT_SEL_1100MV)) {
		puts("tps65217_voltage_update failure\n");
		return;
	}

	/* Set DCDC2 (MPU) voltage */
	if (tps65217_voltage_update(TPS65217_DEFDCDC2, mpu_vdd)) {
		puts("tps65217_voltage_update failure\n");
		return;
	}

	/* Set LDO3, LDO4 output voltage to 3.3V */
	if (tps65217_reg_write(TPS65217_PROT_LEVEL_2,
			       TPS65217_DEFLS1,
			       TPS65217_LDO_VOLTAGE_OUT_3_3,
			       TPS65217_LDO_MASK))
		puts("tps65217_reg_write failure\n");

	if (tps65217_reg_write(TPS65217_PROT_LEVEL_2,
			       TPS65217_DEFLS2,
			       TPS65217_LDO_VOLTAGE_OUT_3_3,
			       TPS65217_LDO_MASK))
		puts("tps65217_reg_write failure\n");
}

void gpi2c_init(void)
{
	/* When needed to be invoked prior to BSS initialization */
	static bool first_time = true;

	if (first_time) {
		enable_i2c0_pin_mux();
		first_time = false;
	}
}

void scale_vcores(void)
{
	struct ctrl_dev *cdev = (struct ctrl_dev *)CTRL_DEVICE_BASE;
	int freq;

	gpi2c_init();
	freq = am335x_get_efuse_mpu_max_freq(cdev);
	scale_vcores_tps65217(freq);
}

void set_uart_mux_conf(void)
{
	enable_uart0_pin_mux();
}

void set_mux_conf_regs(void)
{
	enable_board_pin_mux();
}

const struct ctrl_ioregs ioregs = {
	.cm0ioctl	= MT47H128M16RT25E_IOCTRL_VALUE,
	.cm1ioctl	= MT47H128M16RT25E_IOCTRL_VALUE,
	.cm2ioctl	= MT47H128M16RT25E_IOCTRL_VALUE,
	.dt0ioctl	= MT47H128M16RT25E_IOCTRL_VALUE,
	.dt1ioctl	= MT47H128M16RT25E_IOCTRL_VALUE,
};

void sdram_init(void)
{
	config_ddr(266, &ioregs, &ddr2_data,
		   &ddr2_cmd_ctrl_data, &ddr2_emif_reg_data, 0);
}
#endif

#if defined(CONFIG_OF_BOARD_SETUP) && defined(CONFIG_OF_CONTROL) && \
	defined(CONFIG_DM_ETH) && defined(CONFIG_DRIVER_TI_CPSW)

#define MAX_CPSW_SLAVES	2

/* At the moment, we do not want to stop booting for any failures here */
int ft_board_setup(void *fdt, struct bd_info *bd)
{
	return 0;
}
#endif

int board_init(void)
{
#if defined(CONFIG_HW_WATCHDOG)
	hw_watchdog_init();
#endif
	gd->bd->bi_boot_params = CONFIG_SYS_SDRAM_BASE + 0x100;

	return 0;
}

#ifdef CONFIG_BOARD_LATE_INIT
int board_late_init(void)
{
	return 0;
}
#endif

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	if (!strcmp(name, "am335x-mfc"))
		return 0;

	return -1;
}
#endif

#ifdef CONFIG_TI_SECURE_DEVICE
void board_fit_image_post_process(void **p_image, size_t *p_size)
{
	secure_boot_verify_image(p_image, p_size);
}
#endif

#if !CONFIG_IS_ENABLED(OF_CONTROL)
static const struct omap_hsmmc_plat am335x_mmc0_platdata = {
	.base_addr = (struct hsmmc *)OMAP_HSMMC1_BASE,
	.cfg.host_caps = MMC_MODE_HS_52MHz | MMC_MODE_HS | MMC_MODE_4BIT,
	.cfg.f_min = 400000,
	.cfg.f_max = 52000000,
	.cfg.voltages = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195,
	.cfg.b_max = CONFIG_SYS_MMC_MAX_BLK_COUNT,
};

U_BOOT_DEVICE(am335x_mmc0) = {
	.name = "omap_hsmmc",
	.platdata = &am335x_mmc0_platdata,
};

static const struct omap_hsmmc_plat am335x_mmc1_platdata = {
	.base_addr = (struct hsmmc *)OMAP_HSMMC2_BASE,
	.cfg.host_caps = MMC_MODE_HS_52MHz | MMC_MODE_HS | MMC_MODE_8BIT,
	.cfg.f_min = 400000,
	.cfg.f_max = 52000000,
	.cfg.voltages = MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195,
	.cfg.b_max = CONFIG_SYS_MMC_MAX_BLK_COUNT,
};

U_BOOT_DEVICE(am335x_mmc1) = {
	.name = "omap_hsmmc",
	.platdata = &am335x_mmc1_platdata,
};
#endif
