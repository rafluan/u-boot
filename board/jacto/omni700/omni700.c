// SPDX-License-Identifier: GPL-2.0+

#include <asm/arch/clock.h>
#include <asm/arch/crm_regs.h>
#include <asm/arch/iomux.h>
#include <asm/arch/imx-regs.h>
#include <asm/arch/mx6-pins.h>
#include <asm/arch/sys_proto.h>
#include <asm/gpio.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm/mach-imx/video.h>
#include <asm/io.h>
#include <common.h>
#include <linux/delay.h>

DECLARE_GLOBAL_DATA_PTR;

#define UART_PAD_CTRL  (PAD_CTL_PUS_100K_UP |			\
	PAD_CTL_SPEED_MED | PAD_CTL_DSE_40ohm |			\
	PAD_CTL_SRE_FAST  | PAD_CTL_HYS)

#define LVDS_ENABLE	IMX_GPIO_NR(4, 7)
#define LVDS_PWM	IMX_GPIO_NR(1, 9)
#define BUZZER_GPIO	IMX_GPIO_NR(1, 1)

int dram_init(void)
{
	gd->ram_size = imx_ddr_size();

	return 0;
}

static iomux_v3_cfg_t const uart3_pads[] = {
	IOMUX_PADS(PAD_EIM_D24__UART3_TX_DATA | MUX_PAD_CTRL(UART_PAD_CTRL)),
	IOMUX_PADS(PAD_EIM_D25__UART3_RX_DATA | MUX_PAD_CTRL(UART_PAD_CTRL)),
};

static void setup_iomux_uart(void)
{
	SETUP_IOMUX_PADS(uart3_pads);
}

static iomux_v3_cfg_t const lcd_pads[] = {
	IOMUX_PADS(PAD_KEY_ROW0__GPIO4_IO07 | MUX_PAD_CTRL(NO_PAD_CTRL)),
	IOMUX_PADS(PAD_GPIO_9__GPIO1_IO09 | MUX_PAD_CTRL(NO_PAD_CTRL)),
};

static void setup_iomux_lcd(void)
{
	SETUP_IOMUX_PADS(lcd_pads);
}

static iomux_v3_cfg_t const buzzer_pads[] = {
	IOMUX_PADS(PAD_GPIO_1__GPIO1_IO01 | MUX_PAD_CTRL(NO_PAD_CTRL)),
};

int board_early_init_f(void)
{
	setup_iomux_uart();
	setup_iomux_lcd();
	return 0;
}

#if defined(CONFIG_VIDEO_IPUV3)
static void enable_lvds(struct display_info_t const *dev)
{
	gpio_request(LVDS_ENABLE, "LVDS_ENABLE");
	gpio_direction_output(LVDS_ENABLE, 0);
	gpio_request(LVDS_PWM, "LVDS_PWM");
	gpio_direction_output(LVDS_PWM, 1);
}

struct display_info_t const displays[] = {{
	.bus	= -1,
	.addr	= 0,
	.pixfmt	= IPU_PIX_FMT_RGB666,
	.detect	= NULL,
	.enable	= enable_lvds,
	.mode	= {
		.name           = "ET070WBLG3",
		.refresh        = 60,
		.xres           = 800,
		.yres           = 480,
		.pixclock       = 30066,
		.left_margin    = 40,
		.right_margin   = 33,
		.upper_margin   = 10,
		.lower_margin   = 33,
		.hsync_len      = 128,
		.vsync_len      = 2,
		.sync           = FB_SYNC_EXT,
		.vmode          = FB_VMODE_NONINTERLACED
} } };
size_t display_count = ARRAY_SIZE(displays);

static void setup_display(void)
{
	struct mxc_ccm_reg *mxc_ccm = (struct mxc_ccm_reg *)CCM_BASE_ADDR;
	struct iomuxc *iomux = (struct iomuxc *)IOMUXC_BASE_ADDR;
	int reg;

	enable_ipu_clock();

	/* Turn on LDB0, LDB1, IPU,IPU DI0 clocks */
	reg = readl(&mxc_ccm->CCGR3);
	reg |=  MXC_CCM_CCGR3_LDB_DI0_MASK | MXC_CCM_CCGR3_LDB_DI1_MASK;
	writel(reg, &mxc_ccm->CCGR3);

	/* set LDB0, LDB1 clk select to 011/011 */
	reg = readl(&mxc_ccm->cs2cdr);
	reg &= ~(MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_MASK
		 | MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_MASK);
	reg |= (3 << MXC_CCM_CS2CDR_LDB_DI0_CLK_SEL_OFFSET)
	      | (3 << MXC_CCM_CS2CDR_LDB_DI1_CLK_SEL_OFFSET);
	writel(reg, &mxc_ccm->cs2cdr);

	reg = readl(&mxc_ccm->cscmr2);
	reg |= MXC_CCM_CSCMR2_LDB_DI0_IPU_DIV | MXC_CCM_CSCMR2_LDB_DI1_IPU_DIV;
	writel(reg, &mxc_ccm->cscmr2);

	reg = readl(&mxc_ccm->chsccdr);
	reg |= (CHSCCDR_CLK_SEL_LDB_DI0
		<< MXC_CCM_CHSCCDR_IPU1_DI0_CLK_SEL_OFFSET);
	reg |= (CHSCCDR_CLK_SEL_LDB_DI0
		<< MXC_CCM_CHSCCDR_IPU1_DI1_CLK_SEL_OFFSET);
	writel(reg, &mxc_ccm->chsccdr);

	reg = IOMUXC_GPR2_BGREF_RRMODE_EXTERNAL_RES
	     | IOMUXC_GPR2_DI1_VS_POLARITY_ACTIVE_LOW
	     | IOMUXC_GPR2_DI0_VS_POLARITY_ACTIVE_LOW
	     | IOMUXC_GPR2_BIT_MAPPING_CH1_SPWG
	     | IOMUXC_GPR2_DATA_WIDTH_CH1_18BIT
	     | IOMUXC_GPR2_BIT_MAPPING_CH0_SPWG
	     | IOMUXC_GPR2_DATA_WIDTH_CH0_18BIT
	     | IOMUXC_GPR2_LVDS_CH0_MODE_DISABLED
	     | IOMUXC_GPR2_LVDS_CH1_MODE_ENABLED_DI0;
	writel(reg, &iomux->gpr[2]);

	reg = readl(&iomux->gpr[3]);
	reg = (reg & ~(IOMUXC_GPR3_LVDS1_MUX_CTL_MASK
			| IOMUXC_GPR3_HDMI_MUX_CTL_MASK))
	    | (IOMUXC_GPR3_MUX_SRC_IPU1_DI0
	       << IOMUXC_GPR3_LVDS1_MUX_CTL_OFFSET);
	writel(reg, &iomux->gpr[3]);
}
#endif /* CONFIG_VIDEO_IPUV3 */

int overwrite_console(void)
{
	return 1;
}

static void play_beep(unsigned int duration)
{
	SETUP_IOMUX_PADS(buzzer_pads);
	gpio_request(BUZZER_GPIO, "BUZZER_GPIO");
	gpio_direction_output(BUZZER_GPIO, 1);
	mdelay(duration);
	gpio_direction_output(BUZZER_GPIO, 0);
}

int board_init(void)
{
	gd->bd->bi_boot_params = PHYS_SDRAM + 0x100;

	play_beep(150);

#if defined(CONFIG_VIDEO_IPUV3)
	setup_display();
#endif
	return 0;
}
