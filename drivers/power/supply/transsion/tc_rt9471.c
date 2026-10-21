// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Transsion Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include <linux/power_supply.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/reboot.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/consumer.h>
#include <linux/phy/phy.h>
#include <linux/version.h>
#include <linux/regmap.h>
#include "tc_charger_class.h"
#include "tc_charger.h"
#include "tc_rt9471.h"

#define RT9471_DRV_VERSION	"1.0.17_MTK"
#define RT9471_MANUFACTURER	"Richtek"

#define PHY_MODE_BC11_SET 1
#define PHY_MODE_BC11_CLR 2

#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT) || IS_ENABLED(CONFIG_MTK_HVDCP20_SUPPORT)
int hvdcp20_dp_gpio=0;
bool hvdcp20_dm_0_6_v = false;
#endif
#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT)
int tc30_dm_gpio=0;
#endif

enum rt9471_stat_idx {
	RT9471_STATIDX_STAT0 = 0,
	RT9471_STATIDX_STAT1,
	RT9471_STATIDX_STAT2,
	RT9471_STATIDX_STAT3,
	RT9471_STATIDX_MAX,
};

enum rt9471_irq_idx {
	RT9471_IRQIDX_IRQ0 = 0,
	RT9471_IRQIDX_IRQ1,
	RT9471_IRQIDX_IRQ2,
	RT9471_IRQIDX_IRQ3,
	RT9471_IRQIDX_MAX,
};

enum rt9471_ic_stat {
	RT9471_ICSTAT_SLEEP = 0,
	RT9471_ICSTAT_VBUSRDY,
	RT9471_ICSTAT_TRICKLECHG,
	RT9471_ICSTAT_PRECHG,
	RT9471_ICSTAT_FASTCHG,
	RT9471_ICSTAT_IEOC,
	RT9471_ICSTAT_BGCHG,
	RT9471_ICSTAT_CHGDONE,
	RT9471_ICSTAT_CHGFAULT,
	RT9471_ICSTAT_OTG = 15,
	RT9471_ICSTAT_MAX,
};

static const char * const rt9471_ic_stat_names[RT9471_ICSTAT_MAX] = {
	"hz/sleep", "ready", "trickle-charge", "pre-charge",
	"fast-charge", "ieoc-charge", "background-charge",
	"done", "fault", "RESERVED", "RESERVED", "RESERVED",
	"RESERVED", "RESERVED", "RESERVED", "OTG",
};

enum rt9471_mivr_track {
	RT9471_MIVRTRACK_REG = 0,
	RT9471_MIVRTRACK_VBAT_200MV,
	RT9471_MIVRTRACK_VBAT_250MV,
	RT9471_MIVRTRACK_VBAT_300MV,
	RT9471_MIVRTRACK_MAX,
};

enum rt9471_port_stat {
	RT9471_PORTSTAT_NOINFO = 0,
	RT9471_PORTSTAT_APPLE_10W = 8,
	RT9471_PORTSTAT_SAMSUNG_10W,
	RT9471_PORTSTAT_APPLE_5W,
	RT9471_PORTSTAT_APPLE_12W,
	RT9471_PORTSTAT_NSDP,
	RT9471_PORTSTAT_SDP,
	RT9471_PORTSTAT_CDP,
	RT9471_PORTSTAT_DCP,
	RT9471_PORTSTAT_MAX,
};

enum rt9471_usbsw_state {
	RT9471_USBSW_CHG = 0,
	RT9471_USBSW_USB,
};

enum rt9471_hz_user {
	RT9471_HZU_PP,
	RT9471_HZU_BC12,
	RT9471_HZU_OTG,
	RT9471_HZU_VBUS_GD,
	RT9471_HZU_MAX,
};

static const char * const rt9471_hz_user_names[RT9471_HZU_MAX] = {
	"PP", "BC12", "OTG", "VBUS_GD",
};

struct rt9471_linear_range {
	unsigned int min;
	unsigned int min_sel;
	unsigned int max_sel;
	unsigned int step;
};

struct rt9471_desc {
	const char *chg_name;
	const char *rm_name;
	u8 rm_dev_addr;
	u32 vac_ovp;
	u32 mivr;
	u32 aicr;
	u32 cv;
	u32 ichg;
	u32 ieoc;
	u32 safe_tmr;
	u32 wdt;
	u32 mivr_track;
	u32 bc12_sel;
	bool en_safe_tmr;
	bool en_te;
	bool en_jeita;
	bool ceb_invert;
	bool dis_i2c_tout;
	bool en_qon_rst;
	bool auto_aicr;
};

/* These default values will be applied if there's no property in dts */
static struct rt9471_desc rt9471_default_desc = {
	.chg_name = "primary_chg",
	.rm_name = "rt9471",
	.rm_dev_addr = RT9471_DEVICE_ADDR,
	.vac_ovp = 6500000,
	.mivr = 4500000,
	.aicr = 500000,
	.cv = 4200000,
	.ichg = 2000000,
	.ieoc = 200000,
	.safe_tmr = 10,
	.wdt = 40,
	.mivr_track = RT9471_MIVRTRACK_REG,
	.en_safe_tmr = true,
	.en_te = true,
	.en_jeita = true,
	.ceb_invert = false,
	.dis_i2c_tout = false,
	.en_qon_rst = true,
	.auto_aicr = true,
	.bc12_sel = 0,
};

static const u8 rt9471_irq_maskall[RT9471_IRQIDX_MAX] = {
	0xFF, 0xFF, 0xFF, 0xFF,
};

static const u32 rt9471_vac_ovp[] = {
	5800000, 6500000, 10900000, 14000000,
};

static const u32 rt9471_wdt[] = {
	0, 40, 80, 160,
};

static const u32 rt9471_otgcc[] = {
	500000, 1200000,
};

static const u32 rt9471_otgcv[] = {
	4850000, 5000000,5150000,5300000,
};

static const u8 rt9471_val_en_hidden_mode[] = {
	0x69, 0x96,
};

static const char * const rt9471_port_names[RT9471_PORTSTAT_MAX] = {
	"NOINFO",
	"RESERVED", "RESERVED", "RESERVED", "RESERVED",
	"RESERVED", "RESERVED", "RESERVED",
	"APPLE_10W",
	"SAMSUNG_10W",
	"APPLE_5W",
	"APPLE_12W",
	"NSDP",
	"SDP",
	"CDP",
	"DCP",
};

enum rt9471_fields {
	F_PORT_STAT,
	F_IC_STAT,
	F_CHG_EN,
	/* SAFETY TIMER*/
	F_CHG_TMR, F_CHG_TMR_EN,
	F_VAC_OVP,
	F_VMIVR, F_VMIVR_TRACK,
	F_AUTO_AICR_EN, F_IAICR,
	/* CV, RECHARGE VOLTAGE*/
	F_CV, F_VREC,
	F_CC,
	F_HZ, F_FORCE_HZ,
	F_EOC_RST, F_TE, F_IEOC,
	F_WDT, F_WDT_RST,
	/* DISABLE I2C TIMEOUT */
	F_DIS_I2C_TO,
	/*QON RESET*/
	F_QONRST,
	F_OTG_EN,
	F_OTG_CC,
	F_DP0P6,
	/* BC12 EN*/
	F_BC12_EN,
	/* AICC FUNC */
	F_AICC_EN,
	/* PE1.0/PE2.0 */
	F_PE20_CODE, F_PE10_INC, F_PE_SEL, F_PE_EN,
	F_REGRST,
	F_JEITA,
	/* STATO*/
	F_BC12_DONE, F_CHG_DONE, F_BG_CHG, F_ST_IEOC, F_CHG_RDY, F_VBUSGD,
	/* STAT1*/
	F_ST_MIVR,
	/* STAT2*/
	F_SYSMIN,
	/* STAT3 */
	F_VAC_OV,
	/* HIDDEN_O */
	F_CHIP_REV,
	/* HIDDEN_2 */
	F_OTG_RES_COMP,
	/* TOP_HDEN */
	F_FORCE_EN_VBUS_SINK,

	F_OTG_CV,
	F_MAX_FIELDS
};

static struct reg_field rt9471_reg_fields[F_MAX_FIELDS] = {
	[F_PORT_STAT]	= REG_FIELD(RT9471_REG_STATUS, RT9471_PORTSTAT_SHIFT,
							RT9471_PORTSTAT_SHIFT + 3),
	[F_IC_STAT]	= REG_FIELD(RT9471_REG_STATUS, RT9471_ICSTAT_SHIFT,
							RT9471_ICSTAT_SHIFT + 3),
	[F_CHG_EN]	= REG_FIELD(RT9471_REG_FUNCTION, RT9471_CHG_EN_SHIFT,
							RT9471_CHG_EN_SHIFT),
	[F_CHG_TMR]	= REG_FIELD(RT9471_REG_CHGTIMER, RT9471_SAFETMR_SHIFT,
							RT9471_SAFETMR_SHIFT + 1),
	[F_CHG_TMR_EN]	= REG_FIELD(RT9471_REG_CHGTIMER, RT9471_SAFETMR_EN_SHIFT,
							RT9471_SAFETMR_EN_SHIFT),
	[F_VAC_OVP]	= REG_FIELD(RT9471_REG_VBUS, RT9471_VAC_OVP_SHIFT,
							RT9471_VAC_OVP_SHIFT + 1),
	[F_VMIVR]	= REG_FIELD(RT9471_REG_VBUS, RT9471_MIVR_SHIFT, RT9471_MIVR_SHIFT + 3),
	[F_VMIVR_TRACK] = REG_FIELD(RT9471_REG_VBUS, RT9471_MIVRTRACK_SHIFT,
							RT9471_MIVRTRACK_SHIFT + 1),
	[F_AUTO_AICR_EN] = REG_FIELD(RT9471_REG_IBUS, RT9471_AUTOAICR_SHIFT,
							RT9471_AUTOAICR_SHIFT),
	[F_IAICR]	= REG_FIELD(RT9471_REG_IBUS, RT9471_AICR_SHIFT, RT9471_AICR_SHIFT + 5),
	[F_CV]		= REG_FIELD(RT9471_REG_VCHG, RT9471_CV_SHIFT, RT9471_CV_SHIFT + 6),
	[F_VREC]	= REG_FIELD(RT9471_REG_VCHG, RT9471_VRECHG_SHIFT, RT9471_VRECHG_SHIFT),
	[F_CC]		= REG_FIELD(RT9471_REG_ICHG, RT9471_ICHG_SHIFT, RT9471_ICHG_SHIFT + 5),
	[F_HZ]		= REG_FIELD(RT9471_REG_FUNCTION, RT9471_HZ_SHIFT, RT9471_HZ_SHIFT),
	[F_FORCE_HZ]	= REG_FIELD(RT9471_REG_HIDDEN_2, RT9471_FORCE_HZ_SHIFT,
							RT9471_FORCE_HZ_SHIFT),
	[F_EOC_RST]	= REG_FIELD(RT9471_REG_EOC, RT9471_EOC_RST_SHIFT, RT9471_EOC_RST_SHIFT),
	[F_TE]		= REG_FIELD(RT9471_REG_EOC, RT9471_TE_SHIFT, RT9471_TE_SHIFT),
	[F_IEOC]	= REG_FIELD(RT9471_REG_EOC, RT9471_IEOC_SHIFT, RT9471_IEOC_SHIFT + 3),
	[F_WDT]		= REG_FIELD(RT9471_REG_TOP, RT9471_WDT_SHIFT, RT9471_WDT_SHIFT + 1),
	[F_WDT_RST]	= REG_FIELD(RT9471_REG_TOP, RT9471_WDTCNTRST_SHIFT,
							RT9471_WDTCNTRST_SHIFT),
	[F_DIS_I2C_TO]	= REG_FIELD(RT9471_REG_TOP, RT9471_DISI2CTO_SHIFT, RT9471_DISI2CTO_SHIFT),
	[F_QONRST]	= REG_FIELD(RT9471_REG_TOP, RT9471_QONRST_SHIFT, RT9471_QONRST_SHIFT),
	[F_OTG_EN]	= REG_FIELD(RT9471_REG_FUNCTION, RT9471_OTG_EN_SHIFT, RT9471_OTG_EN_SHIFT),
	[F_OTG_CC]	= REG_FIELD(RT9471_REG_OTGCFG, RT9471_OTGCC_SHIFT, RT9471_OTGCC_SHIFT),
	[F_BC12_EN]	= REG_FIELD(RT9471_REG_DPDMDET, RT9471_BC12_EN_SHIFT,
							RT9471_BC12_EN_SHIFT),
	[F_DP0P6]	= REG_FIELD(RT9471_REG_DPDMDET, RT9471_DP_0_6V_SHIFT, RT9471_DP_0_6V_SHIFT),
	[F_AICC_EN]	= REG_FIELD(RT9471_REG_IBUS, RT9471_AICC_EN_SHIFT, RT9471_AICC_EN_SHIFT),
	[F_PE20_CODE]	= REG_FIELD(RT9471_REG_PUMPEXP, RT9471_PE20_CODE_SHIFT,
							RT9471_PE20_CODE_SHIFT + 4),
	[F_PE10_INC]	= REG_FIELD(RT9471_REG_PUMPEXP, RT9471_PE10_INC_SHIFT,
							RT9471_PE10_INC_SHIFT),
	[F_PE_SEL]	= REG_FIELD(RT9471_REG_PUMPEXP, RT9471_PE_SEL_SHIFT, RT9471_PE_SEL_SHIFT),
	[F_PE_EN]	= REG_FIELD(RT9471_REG_PUMPEXP, RT9471_PE_EN_SHIFT, RT9471_PE_EN_SHIFT),
	[F_REGRST]	= REG_FIELD(RT9471_REG_INFO, RT9471_REGRST_SHIFT, RT9471_REGRST_SHIFT),
	[F_JEITA]	= REG_FIELD(RT9471_REG_JEITA, RT9471_JEITA_EN_SHIFT,
							RT9471_JEITA_EN_SHIFT),
	[F_BC12_DONE]	= REG_FIELD(RT9471_REG_STAT0, RT9471_ST_BC12_DONE_SHIFT,
							RT9471_ST_BC12_DONE_SHIFT),
	[F_CHG_DONE]	= REG_FIELD(RT9471_REG_STAT0, RT9471_ST_CHGDONE_SHIFT,
							RT9471_ST_CHGDONE_SHIFT),
	[F_BG_CHG]	= REG_FIELD(RT9471_REG_STAT0, RT9471_ST_BGCHG_SHIFT,
							RT9471_ST_BGCHG_SHIFT),
	[F_ST_IEOC]	= REG_FIELD(RT9471_REG_STAT0, RT9471_ST_IEOC_SHIFT, RT9471_ST_IEOC_SHIFT),
	[F_CHG_RDY]	= REG_FIELD(RT9471_REG_STAT0, RT9471_ST_CHGRDY_SHIFT,
							RT9471_ST_CHGRDY_SHIFT),
	[F_VBUSGD]	= REG_FIELD(RT9471_REG_STAT0, RT9471_ST_VBUSGD_SHIFT,
							RT9471_ST_VBUSGD_SHIFT),
	[F_ST_MIVR]	= REG_FIELD(RT9471_REG_STAT1, RT9471_ST_MIVR_SHIFT,
							RT9471_ST_MIVR_SHIFT),
	[F_SYSMIN]	= REG_FIELD(RT9471_REG_STAT2, RT9471_ST_SYSMIN_SHIFT,
							RT9471_ST_SYSMIN_SHIFT),
	[F_VAC_OV]	= REG_FIELD(RT9471_REG_STAT3, RT9471_ST_VACOV_SHIFT,
							RT9471_ST_VACOV_SHIFT),
	[F_CHIP_REV]	= REG_FIELD(RT9471_REG_HIDDEN_0, RT9471_CHIP_REV_SHIFT,
							RT9471_CHIP_REV_SHIFT + 2),
	[F_OTG_RES_COMP] = REG_FIELD(RT9471_REG_OTG_HDEN2, RT9471_REG_OTG_RES_COMP_SHIFT,
							RT9471_REG_OTG_RES_COMP_SHIFT),
	[F_FORCE_EN_VBUS_SINK] = REG_FIELD(RT9471_REG_TOP_HDEN, RT9471_FORCE_EN_VBUS_SINK_SHIFT,
							RT9471_FORCE_EN_VBUS_SINK_SHIFT),
	[F_OTG_CV]	= REG_FIELD(RT9471_REG_OTGCFG, RT9471_OTGCV_SHIFT, RT9471_OTGCV_SHIFT+1),
};

enum rt9471_field_ranges {
	RT9471_TMR_RANGES,
	RT9471_VMIVR_RANGES,
	RT9471_IAICR_RANGES,
	RT9471_CV_RANGES,
	RT9471_CC_RANGES,
	RT9471_IEOC_RANGES,
	RT9471_PE20_CODE_RANGES,
	RT9471_MAX_RANGES
};

static const struct rt9471_linear_range rt9471_linear_ranges[RT9471_MAX_RANGES] = {
	[RT9471_TMR_RANGES] = {RT9471_SAFETMR_MIN, 0, 3, RT9471_SAFETMR_STEP},
	[RT9471_VMIVR_RANGES] = {RT9471_MIVR_MIN, 0, 15, RT9471_MIVR_STEP},
	[RT9471_IAICR_RANGES] = {RT9471_AICR_MIN, 1, 63, RT9471_AICR_STEP},
	[RT9471_CV_RANGES]    = {RT9471_CV_MIN, 0, 80, RT9471_CV_STEP},
	[RT9471_CC_RANGES]    = {RT9471_ICHG_MIN, 0, 63, RT9471_ICHG_STEP},
	[RT9471_IEOC_RANGES]  = {RT9471_IEOC_MIN, 0, 15, RT9471_IEOC_STEP},
	[RT9471_PE20_CODE_RANGES] = {RT9471_PE20_CODE_MIN, 0, 29, RT9471_PE20_CODE_STEP},
};

#define RT9471_RM_RAMGE(_fd, _is_linear_range, _linear_range_table, _nonlinear_range_table, _size) \
{ \
	.fd = _fd, \
	.is_linear_range = _is_linear_range, \
	.linear_range_table = _linear_range_table ? _linear_range_table : NULL, \
	.nonlinear_range_table = _nonlinear_range_table ? _nonlinear_range_table : NULL, \
	.nonlinear_table_size = _size, \
}

struct rt9471_rm_range {
	bool is_linear_range; /* is using linear_range */
	u32 fd; /* field index */
	const struct rt9471_linear_range *linear_range_table; /* linear_range table */
	const u32 *nonlinear_range_table; /* not using linear_range table */
	u32 nonlinear_table_size; /* range_table size */
};

static struct rt9471_rm_range rt9471_rm_ranges[] = {
	RT9471_RM_RAMGE(F_CHG_TMR, true, &rt9471_linear_ranges[RT9471_TMR_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_VMIVR, true, &rt9471_linear_ranges[RT9471_VMIVR_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_IAICR, true, &rt9471_linear_ranges[RT9471_IAICR_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_CV, true, &rt9471_linear_ranges[RT9471_CV_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_CC, true, &rt9471_linear_ranges[RT9471_CC_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_IEOC, true, &rt9471_linear_ranges[RT9471_IEOC_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_PE20_CODE, true, &rt9471_linear_ranges[RT9471_PE20_CODE_RANGES], NULL, 0),
	RT9471_RM_RAMGE(F_VAC_OVP, false, NULL, rt9471_vac_ovp, ARRAY_SIZE(rt9471_vac_ovp)),
	RT9471_RM_RAMGE(F_WDT, false, NULL, rt9471_wdt, ARRAY_SIZE(rt9471_wdt)),
	RT9471_RM_RAMGE(F_OTG_CC, false, NULL, rt9471_otgcc, ARRAY_SIZE(rt9471_otgcc)),
	RT9471_RM_RAMGE(F_OTG_CV, false, NULL, rt9471_otgcv, ARRAY_SIZE(rt9471_otgcv)),
};

struct rt9471_chip {
	struct i2c_client *client;
	struct device *dev;
	struct charger_device *chg_dev;
	struct charger_properties chg_props;
	struct mutex io_lock;
	struct mutex bc12_lock;
	struct mutex hidden_mode_lock;
	struct mutex hz_lock;
	int hidden_mode_cnt;
	u8 dev_id;
	u8 dev_rev;
	u8 chip_rev;
	struct rt9471_desc *desc;
	u32 ceb_gpio;
	int irq;
	u8 irq_mask[RT9471_IRQIDX_MAX];
	atomic_t vbus_gd;
	bool attach;
	enum rt9471_port_stat port;
	struct power_supply *psy;
	atomic_t bc12_en;
	wait_queue_head_t bc12_en_req;
	struct wakeup_source *bc12_en_ws;
	struct task_struct *bc12_en_kthread;
	bool chg_done_once;
	struct wakeup_source *buck_dwork_ws;
	struct delayed_work buck_dwork;
	bool enter_shipping_mode;
	struct completion aicc_done;
	struct completion pe_done;
	bool is_primary;
	bool hz_users[RT9471_HZU_MAX];
	struct regulator_dev *otg_rdev;
	struct regulator *regulator;
	struct power_supply_desc psy_desc;
	int psy_usb_type;
	u32 bootmode;
	u32 boottype;
	struct mutex attach_lock;
	bool tcpc_kpoc;
	struct task_struct *attach_task;
	wait_queue_head_t attach_wq;
	atomic_t chrdet_start;
	/*type_c_port0*/
	struct tcpc_device *tcpc_dev;
	struct notifier_block pd_nb;
	struct regmap *rm_dev;
	struct regmap_field *rm_field[F_MAX_FIELDS];
	struct rt9471_rm_range *rm_range[F_MAX_FIELDS];
	bool rt9471_otg_enable_flag;
	atomic_t is_shutdown;
	bool bc12_done;
};

static const u8 rt9471_reg_addr[] = {
	RT9471_REG_OTGCFG,
	RT9471_REG_TOP,
	RT9471_REG_FUNCTION,
	RT9471_REG_IBUS,
	RT9471_REG_VBUS,
	RT9471_REG_PRECHG,
	RT9471_REG_REGU,
	RT9471_REG_VCHG,
	RT9471_REG_ICHG,
	RT9471_REG_CHGTIMER,
	RT9471_REG_EOC,
	RT9471_REG_INFO,
	RT9471_REG_JEITA,
	RT9471_REG_PUMPEXP,
	RT9471_REG_DPDMDET,
	RT9471_REG_STATUS,
	RT9471_REG_STAT0,
	RT9471_REG_STAT1,
	RT9471_REG_STAT2,
	RT9471_REG_STAT3,
	/* Skip IRQs to prevent reading clear while dumping registers */
	RT9471_REG_MASK0,
	RT9471_REG_MASK1,
	RT9471_REG_MASK2,
	RT9471_REG_MASK3,
};

static unsigned int __linear_range_get_max_value(const struct rt9471_linear_range *r)
{
	return r->min + (r->max_sel - r->min_sel) * r->step;
}

static int __linear_range_get_value(const struct rt9471_linear_range *r,
				    unsigned int selector,
				    unsigned int *val)
{
	if (r->min_sel > selector || r->max_sel < selector)
		return -EINVAL;

	*val = r->min + (selector - r->min_sel) * r->step;

	return 0;
}

static void linear_range_get_selector_within(const struct rt9471_linear_range *r,
					     unsigned int val,
					     unsigned int *selector)
{
	if (r->min >= val) {
		*selector = r->min_sel;
		return;
	}

	if (__linear_range_get_max_value(r) <= val) {
		*selector = r->max_sel;
		return;
	}

	if (r->step == 0)
		*selector = r->min_sel;
	else
		*selector = (val - r->min) / r->step + r->min_sel;
}

static inline u8 rt9471_closest_reg_set(struct rt9471_chip *chip,
					enum rt9471_fields fd, u32 target)
{
	int i = 0;
	struct rt9471_rm_range *rm_ranges = chip->rm_range[fd];
	u32 regval = 0, num;

	if ((!rm_ranges->linear_range_table)
			&& (!rm_ranges->nonlinear_range_table)) {
		dev_info(chip->dev, "%s: find rm_range fail\n", __func__);
		return -EINVAL;
	}

	if (rm_ranges->is_linear_range) {
		linear_range_get_selector_within(rm_ranges->linear_range_table,
						target, &regval);
	} else {
		num = rm_ranges->nonlinear_table_size;

		for (i = 0; i < num - 1; i++) {
			if (target >= rm_ranges->nonlinear_range_table[i]
			&& target < rm_ranges->nonlinear_range_table[i + 1]) {
				regval = i;
				break;
			}
		}
		if (i == num - 1)
			regval = num - 1;
	}

	dev_info(chip->dev, "%s target = %d(0x%02X)\n",
						__func__, target, regval);
	return regmap_field_write(chip->rm_field[fd], regval);
}

static inline u32 rt9471_closest_value(struct rt9471_chip *chip,
					enum rt9471_fields fd)
{
	int ret = 0;
	u32 val = 0, regval;
	struct rt9471_rm_range *rm_ranges = chip->rm_range[fd];
	const struct rt9471_linear_range *r = NULL;

	if ((!rm_ranges->linear_range_table)
			&& (!rm_ranges->nonlinear_range_table)) {
		dev_info(chip->dev, "%s: find rm_range fail\n", __func__);
		return -EINVAL;
	}

	dev_info(chip->dev, "%s: fd = %d\n", __func__, fd);

	ret = regmap_field_read(chip->rm_field[fd], &regval);
	if (ret < 0) {
		dev_info(chip->dev, "%s:read fd(%d), fail(%d)\n",
						__func__, fd, ret);
		return ret;
	}

	if (rm_ranges->is_linear_range) {
		r = rm_ranges->linear_range_table;
		ret = __linear_range_get_value(r, regval, &val);
	} else
		val = rm_ranges->nonlinear_range_table[regval];

	return ret < 0 ? ret : val;
}

static int rt9471_enable_hidden_mode(struct rt9471_chip *chip, bool en)
{
	int ret = 0;

	mutex_lock(&chip->hidden_mode_lock);

	if (en) {
		if (chip->hidden_mode_cnt == 0) {
			ret = regmap_bulk_write(chip->rm_dev,
				RT9471_REG_PASSCODE1, rt9471_val_en_hidden_mode,
					ARRAY_SIZE(rt9471_val_en_hidden_mode));
			if (ret < 0)
				goto err;
		}
		chip->hidden_mode_cnt++;
	} else {
		if (chip->hidden_mode_cnt == 1) { /* last one */
			ret = regmap_write(chip->rm_dev,
					RT9471_REG_PASSCODE1, 0x00);
			if (ret < 0)
				goto err;
		}
		chip->hidden_mode_cnt--;
	}
	dev_info(chip->dev, "%s en = %d, cnt = %d\n",
			    __func__, en, chip->hidden_mode_cnt);
	goto out;

err:
	dev_notice(chip->dev, "%s en = %d fail(%d)\n", __func__, en, ret);
out:
	mutex_unlock(&chip->hidden_mode_lock);
	return ret;
}

static int __rt9471_get_ic_stat(struct rt9471_chip *chip,
				enum rt9471_ic_stat *stat)
{
	return regmap_field_read(chip->rm_field[F_IC_STAT], stat);
}

static int __rt9471_get_mivr(struct rt9471_chip *chip, u32 *mivr)
{
	int ret = 0;

	ret = rt9471_closest_value(chip, F_VMIVR);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: get mivr fail\n", __func__);
		return ret;
	}

	*mivr = ret;

	return 0;
}

static int __rt9471_get_aicr(struct rt9471_chip *chip, u32 *aicr)
{
	int ret = 0;

	ret = rt9471_closest_value(chip, F_IAICR);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: get aicr fail\n", __func__);
		return ret;
	}

	if (ret == RT9471_AICR_MAX - RT9471_AICR_STEP)
		ret = RT9471_AICR_MAX;
	*aicr = ret;

	return 0;
}

static int __rt9471_get_cv(struct rt9471_chip *chip, u32 *cv)
{
	int ret = 0;

	ret = rt9471_closest_value(chip, F_CV);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: get mivr fail\n", __func__);
		return ret;
	}

	*cv = ret;

	return 0;
}

static int __rt9471_get_ichg(struct rt9471_chip *chip, u32 *ichg)
{
	int ret = 0;

	ret = rt9471_closest_value(chip, F_CC);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: get mivr fail\n", __func__);
		return ret;
	}

	*ichg = ret;

	return 0;
}

static int __rt9471_get_ieoc(struct rt9471_chip *chip, u32 *ieoc)
{
	int ret = 0;

	ret = rt9471_closest_value(chip, F_IEOC);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: get mivr fail\n", __func__);
		return ret;
	}

	*ieoc = ret;

	return 0;
}

static inline int __rt9471_is_hz_enabled(struct rt9471_chip *chip, bool *en)
{
	int ret = 0;
	u32 regval = 0;

	if (chip->is_primary)
		ret = regmap_field_read(chip->rm_field[F_HZ], &regval);
	else
		ret = regmap_field_read(chip->rm_field[F_FORCE_HZ], &regval);

	if (ret < 0) {
		dev_notice(chip->dev, "%s:(%d) read HZ fail\n", __func__, ret);
		return ret;
	}
	*en = regval;
	return ret;
}

static inline int __rt9471_is_chg_enabled(struct rt9471_chip *chip, bool *en)
{
	int ret = 0;
	u32 regval = 0;

	ret = regmap_field_read(chip->rm_field[F_CHG_EN], &regval);
	if (ret < 0) {
		dev_notice(chip->dev, "%s:(%d) read CHG_EN fail\n", __func__, ret);
		return ret;
	}
	*en = regval;
	return ret;
}

static int rt9471_is_otg_enabled(struct rt9471_chip *chip, bool *en)
{
	int ret = 0;
	u32 regval = 0;

	ret = regmap_field_read(chip->rm_field[F_OTG_EN], &regval);
	if (ret < 0) {
		dev_notice(chip->dev, "%s:(%d) read OTG_EN fail\n", __func__, ret);
		return ret;
	}
	*en = regval;
	return ret;
}

static int __rt9471_enable_shipmode(struct rt9471_chip *chip)
{
	const u8 mask = RT9471_BATFETDIS_MASK | RT9471_HZ_MASK;

	dev_info(chip->dev, "%s\n", __func__);

	return regmap_update_bits(chip->rm_dev,
					RT9471_REG_FUNCTION, mask, mask);
}

static int __rt9471_enable_safe_tmr(struct rt9471_chip *chip, bool en)
{
	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_CHG_TMR_EN], en);
}

static int __rt9471_enable_te(struct rt9471_chip *chip, bool en)
{
	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_TE], en);
}

static int __rt9471_enable_jeita(struct rt9471_chip *chip, bool en)
{
	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_JEITA], en);
}

static int __rt9471_enable_dp_0_6v(struct rt9471_chip *chip, bool en)
{
        dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_DP0P6], en);
}

static int __rt9471_disable_i2c_tout(struct rt9471_chip *chip, bool en)
{
	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_DIS_I2C_TO], en);
}

static int __rt9471_enable_qon_rst(struct rt9471_chip *chip, bool en)
{
	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_QONRST], en);
}

static int __rt9471_enable_autoaicr(struct rt9471_chip *chip, bool en)
{
	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return regmap_field_write(chip->rm_field[F_AUTO_AICR_EN], en);
}

static int __rt9471_enable_hz(struct rt9471_chip *chip, bool en)
{
	int ret = 0;

	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	if (chip->is_primary) {
		ret = regmap_field_write(chip->rm_field[F_HZ], en);
	} else {
		ret = rt9471_enable_hidden_mode(chip, true);
		if (ret < 0)
			goto out;

		ret = regmap_field_write(chip->rm_field[F_FORCE_HZ], en);

		rt9471_enable_hidden_mode(chip, false);
	}
out:
	return ret;
}

static int rt9471_enable_hz(struct rt9471_chip *chip, bool en, u32 user)
{
	int ret = 0, i = 0;

	if (user >= RT9471_HZU_MAX)
		return -EINVAL;

	dev_info(chip->dev, "%s en = %d, user = %s\n",
			    __func__, en, rt9471_hz_user_names[user]);

	mutex_lock(&chip->hz_lock);
	chip->hz_users[user] = en;
	for (i = 0, en = true; i < RT9471_HZU_MAX; i++)
		en &= chip->hz_users[i];
	ret = __rt9471_enable_hz(chip, en);
	mutex_unlock(&chip->hz_lock);

	return ret;
}

static int rt9471_enable_discharge(struct charger_device *chg_dev, bool en);
static int __rt9471_set_wdt(struct rt9471_chip *chip, u32 sec);
static int __rt9471_enable_otg(struct rt9471_chip *chip, bool en)
{
	int ret = 0;

	dev_info(chip->dev, "%s en = %d\n", __func__, en);
	ret = rt9471_enable_discharge(chip->chg_dev, !en);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: disable discharge fail\n", __func__);
		goto out;
	}

	if (en) {
		ret = __rt9471_set_wdt(chip, 0);
		if (ret < 0)
			dev_notice(chip->dev, "%s set wdt fail(%d)\n",
					      __func__, ret);
	}

	ret = rt9471_enable_hz(chip, !en, RT9471_HZU_OTG);
	if (ret < 0) {
		dev_notice(chip->dev, "%s %s hz fail(%d)\n",
				      __func__, en ? "dis" : "en", ret);
		goto out;
	}
	ret = regmap_field_write(chip->rm_field[F_OTG_EN], en);
	if (ret < 0) {
		dev_notice(chip->dev, "%s en otg fail(%d)\n", __func__, ret);
		goto out;
	}
	if (!en) {
		ret = __rt9471_set_wdt(chip, 0);
		if (ret < 0)
			dev_notice(chip->dev, "%s set wdt fail(%d)\n",
					      __func__, ret);
	}
	chip->rt9471_otg_enable_flag = en;
out:
	return ret;
}

static int __rt9471_set_otgcc(struct rt9471_chip *chip, u32 cc)
{
	dev_info(chip->dev, "%s cc = %d\n", __func__, cc);

	if (cc <= rt9471_otgcc[0])
		return regmap_field_write(chip->rm_field[F_OTG_CC], 0x00);
	else
		return regmap_field_write(chip->rm_field[F_OTG_CC], 0x01);
}

static int __rt9471_enable_chg(struct rt9471_chip *chip, bool en)
{
	int ret = 0;
	struct rt9471_desc *desc = chip->desc;

	dev_info(chip->dev, "%s en = %d, chip_rev = %d\n",
			    __func__, en, chip->chip_rev);

	if (chip->ceb_gpio != U32_MAX)
		gpio_set_value(chip->ceb_gpio, desc->ceb_invert ? en : !en);

	ret = regmap_field_write(chip->rm_field[F_CHG_EN], en);
	if (ret < 0)
		dev_notice(chip->dev, "%s: set chg_en %d fail (%d)\n",
							__func__, en, ret);

	if (ret >= 0 && chip->chip_rev <= 4)
		mod_delayed_work(system_wq, &chip->buck_dwork,
				 msecs_to_jiffies(100));

	return ret;
}

static int __rt9471_set_vac_ovp(struct rt9471_chip *chip, u32 vac_ovp)
{
	dev_info(chip->dev, "%s vac_ovp = %d\n", __func__, vac_ovp);

	return rt9471_closest_reg_set(chip, F_VAC_OVP, vac_ovp);
}

static int __rt9471_set_mivr(struct rt9471_chip *chip, u32 mivr)
{
	dev_info(chip->dev, "%s mivr = %d\n", __func__, mivr);

	return rt9471_closest_reg_set(chip, F_VMIVR, mivr);
}

static int __rt9471_set_aicr(struct rt9471_chip *chip, u32 aicr)
{
	dev_info(chip->dev, "%s aicr = %d\n", __func__, aicr);

	return rt9471_closest_reg_set(chip, F_IAICR, aicr);
}

static int __rt9471_set_cv(struct rt9471_chip *chip, u32 cv)
{
	dev_info(chip->dev, "%s cv = %d\n", __func__, cv);

	return rt9471_closest_reg_set(chip, F_CV, cv);
}

static int __rt9471_set_ichg(struct rt9471_chip *chip, u32 ichg)
{
	dev_info(chip->dev, "%s ichg = %d\n", __func__, ichg);

	return rt9471_closest_reg_set(chip, F_CC, ichg);
}

static int __rt9471_set_ieoc(struct rt9471_chip *chip, u32 ieoc)
{
	dev_info(chip->dev, "%s ieoc = %d\n", __func__, ieoc);

	return rt9471_closest_reg_set(chip, F_IEOC, ieoc);
}

static int __rt9471_reset_eoc_state(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);

	return regmap_field_write(chip->rm_field[F_EOC_RST],
						RT9471_EOC_RST_MASK);
}

static int __rt9471_set_safe_tmr(struct rt9471_chip *chip, u32 hr)
{
	dev_info(chip->dev, "%s time = %d\n", __func__, hr);

	return rt9471_closest_reg_set(chip, F_CHG_TMR, hr);
}

static int __rt9471_set_wdt(struct rt9471_chip *chip, u32 sec)
{
	dev_info(chip->dev, "%s time = %d\n", __func__, sec);

	return rt9471_closest_reg_set(chip, F_WDT, sec);
}

static int __rt9471_set_mivrtrack(struct rt9471_chip *chip, u32 mivr_track)
{
	if (mivr_track >= RT9471_MIVRTRACK_MAX)
		mivr_track = RT9471_MIVRTRACK_VBAT_300MV;

	dev_info(chip->dev, "%s mivrtrack = %d\n", __func__, mivr_track);

	return regmap_field_write(chip->rm_field[F_VMIVR_TRACK], mivr_track);
}

static int __rt9471_kick_wdt(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);

	return regmap_field_write(chip->rm_field[F_WDT_RST], 0x01);
}

static inline int rt9471_toggle_bc12(struct rt9471_chip *chip)
{
	int ret = 0;
	u8 regval = 0, bc12_dis[2] = {0}, bc12_en[2] = {0};
	struct i2c_client *client = chip->client;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = bc12_dis,
		},
		{
			.addr = client->addr,
			.flags = 0,
			.len = 2,
			.buf = bc12_en,
		},
	};

	mutex_lock(&chip->io_lock);

	ret = i2c_smbus_read_i2c_block_data(client, RT9471_REG_DPDMDET,
					    1, &regval);
	if (ret < 0) {
		dev_notice(chip->dev, "%s read reg fail(%d)\n", __func__, ret);
		goto out;
	}

	/* bc12 disable and then enable */
	bc12_dis[0] = bc12_en[0] = RT9471_REG_DPDMDET;
	bc12_dis[1] = regval & ~RT9471_BC12_EN_MASK;
	bc12_en[1] = regval | RT9471_BC12_EN_MASK;
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0)
		dev_notice(chip->dev, "%s bc12 dis/en fail(%d)\n",
				      __func__, ret);
out:
	mutex_unlock(&chip->io_lock);
	return ret < 0 ? ret : 0;
}

static int __rt9471_enable_bc12(struct rt9471_chip *chip, bool en)
{
	int ret = 0;

	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	ret = rt9471_enable_hz(chip, !en, RT9471_HZU_BC12);
	if (ret < 0)
		dev_notice(chip->dev, "%s %s hz fail(%d)\n",
				      __func__, en ? "dis" : "en", ret);

	if (en)
		return rt9471_toggle_bc12(chip);
	else
		return regmap_field_write(chip->rm_field[F_BC12_EN], 0x00);
}

#if IS_ENABLED(CONFIG_MTK_HVDCP20_SUPPORT)
static int rt9471_hvdcp20_set_dp_0_6_v(struct charger_device *chg_dev)
{
	int ret =0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	ret = __rt9471_enable_bc12(chip, false);
	if (ret < 0){
		printk("%s dis bc12 fail(%d)\n", __func__, ret);
		return ret;
	}
	msleep(500);

	ret = __rt9471_enable_bc12(chip, true);
	if (ret < 0){
		printk("%s enable bc12 fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = __rt9471_enable_dp_0_6v(chip, true);
	if (ret < 0){
		printk("%s __rt9471_enable_dp_0_6v(%d)\n", __func__, ret);
		return ret;
	}
	msleep(1800);
	printk("%s successfully!\n", __func__);

	return ret;
}

static int rt9471_hvdcp20_set_dp_3_0_v(struct charger_device *chg_dev,bool stat)
{
	int ret = 0;
	
	if(stat) {
		ret = gpio_direction_output(hvdcp20_dp_gpio, 1);
		if (ret < 0) {
			printk("%s: set hvdcp20_dp_gpio fail\n", __func__);
			return -EFAULT;
		}
	} else {
		ret = gpio_direction_output(hvdcp20_dp_gpio, 0);
		if (ret < 0) {
			printk("%s: set hvdcp20_dp_gpio fail\n", __func__);
			return -EFAULT;
		}
	}
	printk("%s %s successfully!\n", __func__,stat?"on":"off");

	return 0;
}

static int rt9471_hvdcp20_set_dm_0_6_v(struct charger_device *chg_dev)
{
	int ret =0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	ret = rt9471_enable_hidden_mode(chip, true);//hidden mode true
	if (ret < 0) {
		printk("%s: enalbe hidden mode fail(%d)\n", __func__, ret);
		return ret;
	}
	ret = regmap_write(chip->rm_dev, RT9471_REG_MANUAL_MODE, 0x01); 
	if (ret < 0){
		printk("%s: enalbe manual mode fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = regmap_write(chip->rm_dev, RT9471_REG_DM_0_6_V, 0x60);
	if (ret < 0){
		printk("%s: set dm 0.6v fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = rt9471_enable_hidden_mode(chip, false);//hidden mode false
	if (ret < 0){
		printk("%s: dis hidden mode fail(%d)\n", __func__, ret);
		return ret;
	}
	hvdcp20_dm_0_6_v = true;
	printk("%s successfully!\n", __func__);

	return ret;
}

static int rt9471_hvdcp20_set_dp_dm_recover(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	ret = __rt9471_enable_dp_0_6v(chip, false);
	if (ret < 0){
		printk("%s: set dp 0.6v false fail(%d)\n", __func__, ret);
		return ret;
	}

	if (hvdcp20_dm_0_6_v == true) {
		ret = rt9471_enable_hidden_mode(chip, true);
		if (ret < 0){
			printk("%s: enalbe hidden mode fail(%d)\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(chip->rm_dev, RT9471_REG_MANUAL_MODE, 0x01);
		if (ret < 0){
			printk("%s: set RT9471_REG_MANUAL_MODE fail(%d)\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(chip->rm_dev, RT9471_REG_DM_0_6_V, 0x00);
		if (ret < 0){
			printk("%s: set RT9471_REG_DM_0_6_V fail(%d)\n", __func__, ret);
			return ret;
		}

		ret = regmap_write(chip->rm_dev, RT9471_REG_MANUAL_MODE, 0x00);
		if (ret < 0){
			printk("%s: set RT9471_REG_MANUAL_MODE fail(%d)\n", __func__, ret);
			return ret;
		}

		ret = rt9471_enable_hidden_mode(chip, false);
		if (ret < 0){
			printk("%s: dis hidden mode fail(%d)\n", __func__, ret);
			return ret;
		}

		ret = rt9471_hvdcp20_set_dp_3_0_v(chg_dev, false);
		if (ret < 0){
			printk("%s: set dp 3.0v false fail(%d)\n", __func__, ret);
			return ret;
		}

		#if defined(CONFIG_MTK_PMIC_CHIP_MT6358)
		ret = mt6358_upmu_set_rg_vusb_vocal(0x3);//3.00v
		if (ret < 0){
			printk("%s: set vubs 3.0v fail(%d)\n", __func__, ret);
			return ret;
		}

		ret = mt6358_upmu_set_rg_vusb_vosel(0x7);//0.07v
		if (ret < 0){
			printk("%s: set vubs 0.07v fail(%d)\n", __func__, ret);
			return ret;
		}
		#endif

		hvdcp20_dm_0_6_v = false;
	}
	printk("%s successfully!\n", __func__);

	return ret;
}
#endif
#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT)
static int rt9471_tc30_gpio_enable(struct charger_device *chg_dev,bool stat)
{
	int ret = 0;
	
	if(stat) {
		ret = gpio_direction_output(hvdcp20_dp_gpio, 1);
		if (ret < 0) {
			printk("%s: set hvdcp20_dp_gpio fail\n", __func__);
			return -EFAULT;
		}
		ret = gpio_direction_output(tc30_dm_gpio, 1);
		if (ret < 0) {
			printk("%s: set tc30_dm_gpio fail\n", __func__);
			return -EFAULT;
		}
	} else {
		ret = gpio_direction_output(hvdcp20_dp_gpio, 0);
		if (ret < 0) {
			printk("%s: set hvdcp20_dp_gpio fail\n", __func__);
			return -EFAULT;
		}
		ret = gpio_direction_output(tc30_dm_gpio, 0);
		if (ret < 0) {
			printk("%s: set tc30_dm_gpio fail\n", __func__);
			return -EFAULT;
		}
	}
	printk("%s %s successfully!\n", __func__,stat?"on":"off");

	return 0;
}
#endif

static int __rt9471_dump_registers(struct rt9471_chip *chip)
{
	int ret = 0, i = 0;
	u32 mivr = 0, aicr = 0, cv = 0, ichg = 0, ieoc = 0;
	bool chg_en = 0;
	enum rt9471_ic_stat ic_stat = RT9471_ICSTAT_SLEEP;
	u8 stats[RT9471_STATIDX_MAX] = {0}, regval = 0, hidden_2 = 0;
	union power_supply_propval val = {.intval = 0};

	ret = __rt9471_kick_wdt(chip);

	ret |= power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &val);
	mivr = val.intval;
	ret |= power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	aicr = val.intval;
	ret |= power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	cv = val.intval;
	ret |= power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
	ichg = val.intval;
	ret |= power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, &val);
	ieoc = val.intval;
	ret |= __rt9471_is_chg_enabled(chip, &chg_en);
	ret |= __rt9471_get_ic_stat(chip, &ic_stat);
	if (ret)
		dev_notice(chip->dev, " %s: (%d) Failed to get settings\n",
								__func__, ret);

	ret = regmap_bulk_read(chip->rm_dev,
			RT9471_REG_STAT0, stats, RT9471_STATIDX_MAX);
	ret |= regmap_raw_read(chip->rm_dev,
			RT9471_REG_HIDDEN_2, &hidden_2, sizeof(hidden_2));
	if (ret)
		dev_notice(chip->dev, " %s: (%d) Failed to read status\n",
								__func__, ret);

	if (ic_stat == RT9471_ICSTAT_CHGFAULT) {
		for (i = 0; i < ARRAY_SIZE(rt9471_reg_addr); i++) {
			ret = regmap_raw_read(chip->rm_dev, rt9471_reg_addr[i],
						   &regval, sizeof(regval));
			if (ret < 0)
				continue;
			dev_notice(chip->dev, "%s reg0x%02X = 0x%02X\n",
					      __func__, rt9471_reg_addr[i],
					      regval);
		}
	}

	dev_info(chip->dev, "%s MIVR = %dmV, AICR = %dmA\n",
		 __func__, mivr / 1000, aicr / 1000);

	dev_info(chip->dev, "%s CV = %dmV, ICHG = %dmA, IEOC = %dmA\n",
		 __func__, cv / 1000, ichg / 1000, ieoc / 1000);

	dev_info(chip->dev, "%s CHG_EN = %d, IC_STAT = %s\n",
		 __func__, chg_en, rt9471_ic_stat_names[ic_stat]);

	dev_info(chip->dev, "%s STAT0 = 0x%02X, STAT1 = 0x%02X\n", __func__,
		 stats[RT9471_STATIDX_STAT0], stats[RT9471_STATIDX_STAT1]);

	dev_info(chip->dev, "%s STAT2 = 0x%02X, STAT3 = 0x%02X\n", __func__,
		 stats[RT9471_STATIDX_STAT2], stats[RT9471_STATIDX_STAT3]);

	dev_info(chip->dev, "%s HIDDEN_2 = 0x%02X\n", __func__, hidden_2);

	return 0;
}

static void rt9471_buck_dwork_handler(struct work_struct *work)
{
	int ret = 0, i = 0;
	struct rt9471_chip *chip =
		container_of(work, struct rt9471_chip, buck_dwork.work);
	bool chg_rdy = false, chg_done = false;
	u32 sys_min = 0;
	u8 regval = 0;
	u8 reg_addrs[] = {RT9471_REG_BUCK_HDEN4, RT9471_REG_BUCK_HDEN1,
			  RT9471_REG_BUCK_HDEN2, RT9471_REG_BUCK_HDEN4,
			  RT9471_REG_BUCK_HDEN2, RT9471_REG_BUCK_HDEN1};
	u8 reg_vals[] = {0x77, 0x2F, 0xA2, 0x71, 0x22, 0x2D};

	dev_info(chip->dev, "%s\n", __func__);

	__pm_stay_awake(chip->buck_dwork_ws);

	ret = regmap_raw_read(chip->rm_dev, RT9471_REG_STAT0,
						&regval, sizeof(regval));
	if (ret < 0)
		goto out;
	chg_rdy = (regval & RT9471_ST_CHGRDY_MASK ? true : false);
	chg_done = (regval & RT9471_ST_CHGDONE_MASK ? true : false);
	dev_info(chip->dev, "%s chg_rdy = %d\n", __func__, chg_rdy);
	dev_info(chip->dev, "%s chg_done = %d, chg_done_once = %d\n",
			    __func__, chg_done, chip->chg_done_once);
	if (!chg_rdy)
		goto out;

	ret = regmap_field_read(chip->rm_field[F_SYSMIN], &sys_min);
	if (ret < 0)
		goto out;
	dev_info(chip->dev, "%s sys_min = %d\n", __func__, sys_min);
	/* Should not enter CV tracking in sys_min */
	if (sys_min)
		reg_vals[1] = 0x2D;

	ret = rt9471_enable_hidden_mode(chip, true);
	if (ret < 0)
		goto out;

	for (i = 0; i < ARRAY_SIZE(reg_addrs); i++) {
		ret = regmap_write(chip->rm_dev, reg_addrs[i], reg_vals[i]);
		if (ret < 0)
			dev_notice(chip->dev,
				   "%s reg0x%02X = 0x%02X fail(%d)\n",
				   __func__, reg_addrs[i], reg_vals[i], ret);
		if (i == 1)
			udelay(1000);
	}

	rt9471_enable_hidden_mode(chip, false);

	if (chg_done && !chip->chg_done_once) {
		chip->chg_done_once = true;
		mod_delayed_work(system_wq, &chip->buck_dwork,
				 msecs_to_jiffies(100));
	}
out:
	__pm_relax(chip->buck_dwork_ws);
}

static bool rt9471_is_vbus_gd(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 regval = 0;
	bool vbus_gd = false;

	ret = regmap_field_read(chip->rm_field[F_VBUSGD], &regval);
	if (ret < 0)
		dev_notice(chip->dev, "%s check stat fail(%d)\n",
				      __func__, ret);
	vbus_gd = regval;
	dev_dbg(chip->dev, "%s vbus_gd = %d\n", __func__, vbus_gd);

	return vbus_gd;
}

static int rt9471_set_usbsw_state(struct rt9471_chip *chip, int usbsw)
{
	struct phy *phy;
	int ret, mode = (usbsw == RT9471_USBSW_CHG) ? PHY_MODE_BC11_SET :
					       PHY_MODE_BC11_CLR;

	dev_info(chip->dev, "usbsw=%d\n", usbsw);
	phy = phy_get(chip->dev, "usb2-phy");
	if (IS_ERR_OR_NULL(phy)) {
		dev_notice(chip->dev, "failed to get usb2-phy\n");
		return -ENODEV;
	}
	ret = phy_set_mode_ext(phy, PHY_MODE_USB_DEVICE, mode);
	if (ret)
		dev_notice(chip->dev, "failed to set phy ext mode\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	phy_put(chip->dev, phy);
#else
	phy_put(phy);
#endif
	return ret;
}

static bool is_usb_rdy(struct device *dev)
{
	bool ready = true;
	struct device_node *node = NULL;

	node = of_parse_phandle(dev->of_node, "usb", 0);
	if (node) {
		ready = !of_property_read_bool(node, "cdp-block");
		dev_info(dev, "usb ready = %d\n", ready);
	} else
		dev_notice(dev, "usb node missing or invalid\n");
	return ready;
}

static int rt9471_bc12_en_kthread(void *data)
{
	int ret = 0, i = 0, en = 0;
	struct rt9471_chip *chip = data;
	const int max_wait_cnt = 200;

	dev_info(chip->dev, "%s\n", __func__);
wait:
	wait_event_interruptible(chip->bc12_en_req, atomic_read(&chip->bc12_en) >= 0 ||
				      kthread_should_stop());
	if (kthread_should_stop()) {
		dev_info(chip->dev, "%s bye\n", __func__);
		return 0;
	}

	en = atomic_xchg(&chip->bc12_en, -1);

	dev_info(chip->dev, "%s en = %d\n", __func__, en);
	if (en < 0)
		goto wait;

	__pm_stay_awake(chip->bc12_en_ws);

	if (en) {
		/* Workaround for CDP port */
		for (i = 0; i < max_wait_cnt && !is_usb_rdy(chip->dev); i++) {
			dev_dbg(chip->dev, "%s CDP block\n", __func__);
			if (!atomic_read(&chip->vbus_gd) || atomic_read(&chip->is_shutdown)) {
				dev_info(chip->dev, "%s plug out or shutdown\n", __func__);
				goto relax_and_wait;
			}
			msleep(100);
		}
		if (i >= max_wait_cnt)
			dev_notice(chip->dev, "%s CDP timeout\n", __func__);
		else
			dev_info(chip->dev, "%s CDP free\n", __func__);
	}
	rt9471_set_usbsw_state(chip, en ? RT9471_USBSW_CHG : RT9471_USBSW_USB);
	ret = __rt9471_enable_bc12(chip, en);
	if (ret < 0)
		dev_notice(chip->dev, "%s en = %d fail(%d)\n",
				      __func__, en, ret);
relax_and_wait:
	__pm_relax(chip->bc12_en_ws);
	goto wait;

	return 0;
}

static void rt9471_enable_bc12(struct rt9471_chip *chip, bool en)
{
	if (chip->desc->bc12_sel)
		return;

	dev_info(chip->dev, "%s en = %d\n", __func__, en);
	atomic_set(&chip->bc12_en, en);
	wake_up(&chip->bc12_en_req);
}

static int rt9471_bc12_preprocess(struct rt9471_chip *chip)
{
	rt9471_enable_bc12(chip, true);
	return 0;
}

static int  rt9471_bc12_postprocess(struct rt9471_chip *chip)
{
	int ret = 0;
	bool attach = false, inform_psy = true;
	u32 port = RT9471_PORTSTAT_NOINFO;

#if IS_ENABLED(CONFIG_WIRELESS_TA)
	struct power_supply *psy;
	union power_supply_propval prop;

	psy = power_supply_get_by_name("wireless");
	if (!psy) {
		dev_err(chip->dev,"%s: get wireless power supply failed\n", __func__);
	}
#endif

	attach = atomic_read(&chip->vbus_gd);
	if (chip->attach == attach) {
		dev_info(chip->dev, "%s attach(%d) is the same\n",
				    __func__, attach);
		inform_psy = !attach;
		goto out;
	}
	chip->attach = attach;

#if IS_ENABLED(CONFIG_WIRELESS_TA)
	if (!IS_ERR_OR_NULL(psy)) {
		ret = power_supply_get_property(psy,POWER_SUPPLY_PROP_ONLINE, &prop);
		if(prop.intval){
			rt9471_enable_bc12(chip, false);
			dev_info(chip->dev, "%s attach(%d) wireless charger\n",__func__, attach);
			return 0;
		}
	}
#endif

	chip->port = RT9471_PORTSTAT_NOINFO;
	chip->psy_desc.type = POWER_SUPPLY_TYPE_USB;
	chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	dev_info(chip->dev, "%s attach = %d\n", __func__, attach);

	if (!attach || chip->rt9471_otg_enable_flag)
		goto out;

	ret = regmap_field_read(chip->rm_field[F_PORT_STAT], &port);
	if (ret >= 0)
		chip->port = port;

	switch (chip->port) {
	case RT9471_PORTSTAT_NOINFO:
		break;
	case RT9471_PORTSTAT_SDP:
		chip->psy_desc.type = POWER_SUPPLY_TYPE_USB;
		chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		break;
	case RT9471_PORTSTAT_CDP:
		chip->psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_CDP;
		break;
	case RT9471_PORTSTAT_DCP:
	case RT9471_PORTSTAT_SAMSUNG_10W:
	case RT9471_PORTSTAT_APPLE_12W:
	case RT9471_PORTSTAT_APPLE_10W:
	case RT9471_PORTSTAT_APPLE_5W:
		chip->psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;

	case RT9471_PORTSTAT_NSDP:
	default:
		chip->psy_desc.type = POWER_SUPPLY_TYPE_USB;
		chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_DCP;
		break;
	}
out:
	if (chip->psy_desc.type != POWER_SUPPLY_TYPE_USB_DCP)
		rt9471_enable_bc12(chip, false);
	if (inform_psy)
		power_supply_changed(chip->psy);

	return 0;
}

static int rt9471_detach_irq_handler(struct rt9471_chip *chip)
{
	bool vbus_gd = rt9471_is_vbus_gd(chip);

	dev_info(chip->dev, "%s vbus_gd = %d\n", __func__, vbus_gd);
	if (vbus_gd)
		goto out;

	complete(&chip->aicc_done);
	complete(&chip->pe_done);
#if !IS_ENABLED(CONFIG_TCPC_CLASS)
	if (chip->desc->bc12_sel)
		goto out;
	if (chip->dev_id != RT9470D_DEVID && chip->dev_id != RT9471D_DEVID)
		goto out;
	mutex_lock(&chip->bc12_lock);
	atomic_set(&chip->vbus_gd, false);
	rt9471_bc12_postprocess(chip);
	mutex_unlock(&chip->bc12_lock);
#endif /* CONFIG_TCPC_CLASS */
out:
	return 0;
}

static int rt9471_rechg_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	power_supply_changed(chip->psy);
	return 0;
}

static void rt9471_bc12_done_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 bc12_done = 0, chg_rdy = 0;

	if (chip->desc->bc12_sel)
		return;

	if (chip->dev_id != RT9470D_DEVID && chip->dev_id != RT9471D_DEVID)
		return;

	dev_info(chip->dev, "%s\n", __func__);

	ret = regmap_field_read(chip->rm_field[F_BC12_DONE], &bc12_done);
	if (ret < 0)
		return;
	ret = regmap_field_read(chip->rm_field[F_CHG_RDY], &chg_rdy);
	if (ret < 0)
		return;

	dev_info(chip->dev, "%s bc12_done = %d, chg_rdy = %d, chip_rev = %d\n",
			    __func__, bc12_done, chg_rdy, chip->chip_rev);
	chip->bc12_done = !!bc12_done;
	if (bc12_done) {
		if (chip->chip_rev <= 3 && !chg_rdy) {
			/* Workaround waiting for chg_rdy */
			dev_info(chip->dev, "%s wait chg_rdy\n", __func__);
			return;
		}
		mutex_lock(&chip->bc12_lock);
		rt9471_bc12_postprocess(chip);
		dev_info(chip->dev, "%s %d %s\n", __func__, chip->port,
				    rt9471_port_names[chip->port]);
		mutex_unlock(&chip->bc12_lock);
	}
}

static int rt9471_bc12_done_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	rt9471_bc12_done_handler(chip);
	return 0;
}

static int rt9471_chg_done_irq_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 chg_done = 0;

	ret = regmap_field_read(chip->rm_field[F_CHG_DONE], &chg_done);
	if (ret < 0)
		dev_notice(chip->dev, "%s check stat fail(%d)\n",
				      __func__, ret);
	dev_info(chip->dev, "%s chg_done = %d, chip_rev = %d\n",
			    __func__, chg_done, chip->chip_rev);
	if (!chg_done || chip->chip_rev > 4)
		goto out;

	cancel_delayed_work_sync(&chip->buck_dwork);
	chip->chg_done_once = false;
	mod_delayed_work(system_wq, &chip->buck_dwork, msecs_to_jiffies(100));
out:
	return 0;
}

static int rt9471_bg_chg_irq_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 bg_chg = 0;

	ret = regmap_field_read(chip->rm_field[F_BG_CHG], &bg_chg);
	if (ret < 0)
		dev_notice(chip->dev, "%s check stat fail(%d)\n",
				      __func__, ret);
	dev_info(chip->dev, "%s bg_chg = %d\n", __func__, bg_chg);

	return 0;
}

static int rt9471_ieoc_irq_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 ieoc = 0;

	ret = regmap_field_read(chip->rm_field[F_ST_IEOC], &ieoc);
	if (ret < 0)
		dev_notice(chip->dev, "%s check stat fail(%d)\n",
				      __func__, ret);
	dev_info(chip->dev, "%s ieoc = %d\n", __func__, ieoc);
	if (!ieoc)
		goto out;

	power_supply_changed(chip->psy);
out:
	return 0;
}

static int rt9471_chg_rdy_irq_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 chg_rdy = false;

	ret = regmap_field_read(chip->rm_field[F_CHG_RDY], &chg_rdy);
	if (ret < 0)
		dev_notice(chip->dev, "%s check stat fail(%d)\n",
				      __func__, ret);
	dev_info(chip->dev, "%s chg_rdy = %d, chip_rev = %d\n",
			    __func__, chg_rdy, chip->chip_rev);
	if (!chg_rdy || chip->chip_rev > 4)
		goto out;

	if (chip->chip_rev <= 3)
		rt9471_bc12_done_handler(chip);
	mod_delayed_work(system_wq, &chip->buck_dwork, msecs_to_jiffies(100));
out:
	return 0;
}

static int rt9471_vbus_gd_irq_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	bool vbus_gd = false;
	struct chgdev_notify *noti = &(chip->chg_dev->noti);

#if !IS_ENABLED(CONFIG_TCPC_CLASS)
	bool attach = false;

	mutex_lock(&chip->bc12_lock);
	attach = atomic_read(&chip->vbus_gd);
	mutex_unlock(&chip->bc12_lock);

	if (!attach) {
		ret = rt9471_enable_hz(chip, false, RT9471_HZU_VBUS_GD);
		if (ret < 0)
			dev_notice(chip->dev, "%s disable hz fail(%d)\n", __func__, ret);
	}
#endif /* CONFIG_TCPC_CLASS */
	vbus_gd = rt9471_is_vbus_gd(chip);

	dev_info(chip->dev, "%s vbus_gd = %d\n", __func__, vbus_gd);
	if (!vbus_gd)
		goto out;

	noti->vbusov_stat = false;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);
#if !IS_ENABLED(CONFIG_TCPC_CLASS)
	if (chip->desc->bc12_sel)
		goto out;
	if (chip->dev_id != RT9470D_DEVID && chip->dev_id != RT9471D_DEVID)
		goto out;
	mutex_lock(&chip->bc12_lock);
	atomic_set(&chip->vbus_gd, true);
	rt9471_bc12_preprocess(chip);
	mutex_unlock(&chip->bc12_lock);
#endif /* CONFIG_TCPC_CLASS */
out:
	ret = rt9471_enable_hz(chip, true, RT9471_HZU_VBUS_GD);
        if (ret < 0)
                dev_notice(chip->dev, "%s en hz fail(%d)\n", __func__, ret);

	return ret;
}

static int rt9471_chg_batov_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	power_supply_changed(chip->psy);
	return 0;
}

static int rt9471_chg_sysov_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_chg_tout_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_SAFETY_TIMEOUT);
	return 0;
}

static int rt9471_chg_busuv_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_chg_threg_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_chg_aicr_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_chg_mivr_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_sys_short_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_sys_min_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_aicc_done_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	complete(&chip->aicc_done);
	return 0;
}

static int rt9471_pe_done_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	complete(&chip->pe_done);
	return 0;
}

static int rt9471_jeita_cold_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_jeita_cool_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_jeita_warm_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_jeita_hot_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_otg_fault_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_OTG_FAULT);
	return 0;
}

static int rt9471_otg_lbp_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_otg_cc_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

static int rt9471_wdt_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return __rt9471_kick_wdt(chip);
}

static int rt9471_vac_ov_irq_handler(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 vac_ov = 0;
	struct chgdev_notify *noti = &(chip->chg_dev->noti);

	ret = regmap_field_read(chip->rm_field[F_VAC_OV], &vac_ov);
	if (ret < 0)
		dev_notice(chip->dev, "%s check stat fail(%d)\n",
				      __func__, ret);
	dev_info(chip->dev, "%s vac_ov = %d\n", __func__, vac_ov);

	noti->vbusov_stat = vac_ov;
	charger_dev_notify(chip->chg_dev, CHARGER_DEV_NOTIFY_VBUS_OVP);
	return 0;
}

static int rt9471_otp_irq_handler(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);
	return 0;
}

struct irq_mapping_tbl {
	const char *name;
	int (*hdlr)(struct rt9471_chip *chip);
	int num;
};

#define RT9471_IRQ_MAPPING(_name, _num) \
	{.name = #_name, .hdlr = rt9471_##_name##_irq_handler, .num = _num}

static const struct irq_mapping_tbl rt9471_irq_mapping_tbl[] = {
	RT9471_IRQ_MAPPING(wdt, 29),
	RT9471_IRQ_MAPPING(vbus_gd, 7),
	RT9471_IRQ_MAPPING(chg_rdy, 6),
	RT9471_IRQ_MAPPING(bc12_done, 0),
	RT9471_IRQ_MAPPING(detach, 1),
	RT9471_IRQ_MAPPING(rechg, 2),
	RT9471_IRQ_MAPPING(chg_done, 3),
	RT9471_IRQ_MAPPING(bg_chg, 4),
	RT9471_IRQ_MAPPING(ieoc, 5),
	RT9471_IRQ_MAPPING(chg_batov, 9),
	RT9471_IRQ_MAPPING(chg_sysov, 10),
	RT9471_IRQ_MAPPING(chg_tout, 11),
	RT9471_IRQ_MAPPING(chg_busuv, 12),
	RT9471_IRQ_MAPPING(chg_threg, 13),
	RT9471_IRQ_MAPPING(chg_aicr, 14),
	RT9471_IRQ_MAPPING(chg_mivr, 15),
	RT9471_IRQ_MAPPING(sys_short, 16),
	RT9471_IRQ_MAPPING(sys_min, 17),
	RT9471_IRQ_MAPPING(aicc_done, 18),
	RT9471_IRQ_MAPPING(pe_done, 19),
	RT9471_IRQ_MAPPING(jeita_cold, 20),
	RT9471_IRQ_MAPPING(jeita_cool, 21),
	RT9471_IRQ_MAPPING(jeita_warm, 22),
	RT9471_IRQ_MAPPING(jeita_hot, 23),
	RT9471_IRQ_MAPPING(otg_fault, 24),
	RT9471_IRQ_MAPPING(otg_lbp, 25),
	RT9471_IRQ_MAPPING(otg_cc, 26),
	RT9471_IRQ_MAPPING(vac_ov, 30),
	RT9471_IRQ_MAPPING(otp, 31),
};
static irqreturn_t rt9471_irq_handler(int irq, void *data)
{
	int ret = 0, i = 0, irqnum = 0, irqbit = 0;
	u8 evt[RT9471_IRQIDX_MAX] = {0};
	u8 mask[RT9471_IRQIDX_MAX] = {0};
	struct rt9471_chip *chip = (struct rt9471_chip *)data;

	dev_info(chip->dev, "%s\n", __func__);

	pm_stay_awake(chip->dev);

	ret = regmap_bulk_read(chip->rm_dev, RT9471_REG_IRQ0,
						evt, RT9471_IRQIDX_MAX);
	if (ret < 0) {
		dev_notice(chip->dev, "%s read evt fail(%d)\n", __func__, ret);
		goto out;
	}

	ret = regmap_bulk_read(chip->rm_dev, RT9471_REG_MASK0,
						mask, RT9471_IRQIDX_MAX);
	if (ret < 0) {
		dev_notice(chip->dev, "%s read mask fail(%d)\n", __func__, ret);
		goto out;
	}

	for (i = 0; i < RT9471_IRQIDX_MAX; i++)
		evt[i] &= ~mask[i];
	for (i = 0; i < ARRAY_SIZE(rt9471_irq_mapping_tbl); i++) {
		irqnum = rt9471_irq_mapping_tbl[i].num / 8;
		if (irqnum >= RT9471_IRQIDX_MAX)
			continue;
		irqbit = rt9471_irq_mapping_tbl[i].num % 8;
		if (evt[irqnum] & (1 << irqbit))
			rt9471_irq_mapping_tbl[i].hdlr(chip);
	}
out:
	pm_relax(chip->dev);
	return IRQ_HANDLED;
}

static int rt9471_register_irq(struct rt9471_chip *chip)
{
	int ret = 0;

	dev_info(chip->dev, "%s irq = %d\n", __func__, chip->client->irq);

	/* Request threaded IRQ */
	ret = devm_request_threaded_irq(chip->dev, chip->client->irq, NULL,
					rt9471_irq_handler, IRQF_ONESHOT,
					dev_name(chip->dev), chip);
	if (ret) {
		dev_notice(chip->dev, "%s request threaded irq fail(%d)\n",
				      __func__, ret);
		return ret;
	}
	enable_irq_wake(chip->client->irq);
	device_init_wakeup(chip->dev, true);
	return 0;
}

static int rt9471_init_irq(struct rt9471_chip *chip)
{
	dev_info(chip->dev, "%s\n", __func__);

	return regmap_bulk_write(chip->rm_dev, RT9471_REG_MASK0,
				chip->irq_mask, ARRAY_SIZE(chip->irq_mask));
}

static inline int rt9471_get_irq_number(struct rt9471_chip *chip,
					const char *name)
{
	int i = 0;

	if (!name) {
		dev_notice(chip->dev, "%s null name\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(rt9471_irq_mapping_tbl); i++) {
		if (!strcmp(name, rt9471_irq_mapping_tbl[i].name))
			return rt9471_irq_mapping_tbl[i].num;
	}

	return -EINVAL;
}

static inline const char *rt9471_get_irq_name(int irqnum)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(rt9471_irq_mapping_tbl); i++) {
		if (rt9471_irq_mapping_tbl[i].num == irqnum)
			return rt9471_irq_mapping_tbl[i].name;
	}

	return "not found";
}

static inline void rt9471_irq_mask(struct rt9471_chip *chip, int irqnum)
{
	dev_dbg(chip->dev, "%s irq(%d, %s)\n", __func__, irqnum,
		rt9471_get_irq_name(irqnum));
	chip->irq_mask[irqnum / 8] |= (1 << (irqnum % 8));
}

static inline void rt9471_irq_unmask(struct rt9471_chip *chip, int irqnum)
{
	dev_info(chip->dev, "%s irq(%d, %s)\n", __func__, irqnum,
		 rt9471_get_irq_name(irqnum));
	chip->irq_mask[irqnum / 8] &= ~(1 << (irqnum % 8));
}

static int rt9471_parse_dt(struct rt9471_chip *chip)
{
	int ret = 0, irqcnt = 0, irqnum = 0;
	struct device_node *parent_np = chip->dev->of_node, *np = NULL;
	struct rt9471_desc *desc = NULL;
	const char *name = NULL;

	dev_info(chip->dev, "%s\n", __func__);

	chip->desc = &rt9471_default_desc;

	if (!parent_np) {
		dev_notice(chip->dev, "%s no device node\n", __func__);
		return -EINVAL;
	}
	np = of_get_child_by_name(parent_np, "rt9471");
	if (!np) {
		dev_info(chip->dev, "%s no rt9471 device node\n", __func__);
		np = parent_np;
	}

	desc = devm_kmemdup(chip->dev, &rt9471_default_desc,
			    sizeof(rt9471_default_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	chip->desc = desc;

	ret = of_property_read_string(np, "chg_name", &desc->chg_name);
	if (ret < 0)
		dev_info(chip->dev, "%s no chg_name(%d)\n", __func__, ret);

	ret = of_property_read_string(np, "chg_alias_name",
				      &chip->chg_props.alias_name);
	if (ret < 0) {
		dev_info(chip->dev, "%s no chg_alias_name(%d)\n",
				    __func__, ret);
		chip->chg_props.alias_name = "rt9471_chg";
	}
	dev_info(chip->dev, "%s name = %s, alias name = %s\n", __func__,
			    desc->chg_name, chip->chg_props.alias_name);

	if (strcmp(desc->chg_name, "primary_chg") == 0)
		chip->is_primary = true;

#if !defined(CONFIG_MTK_GPIO) || defined(CONFIG_MTK_GPIOLIB_STAND)
	ret = of_get_named_gpio(parent_np, "rt,ceb_gpio", 0);
	if (ret < 0) {
		dev_info(chip->dev, "%s no rt,ceb_gpio(%d)\n",
				    __func__, ret);
		chip->ceb_gpio = U32_MAX;
	} else
		chip->ceb_gpio = ret;
#else
	ret = of_property_read_u32(parent_np, "rt,ceb_gpio_num",
				   &chip->ceb_gpio);
	if (ret < 0) {
		dev_info(chip->dev, "%s no rt,ceb_gpio_num(%d)\n",
				    __func__, ret);
		chip->ceb_gpio = U32_MAX;
	}
#endif
	dev_info(chip->dev, "%s ceb_gpio = %u\n",
			    __func__, chip->ceb_gpio);

	if (strcmp(desc->chg_name, "primary_chg") == 0) {

#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT) || IS_ENABLED(CONFIG_MTK_HVDCP20_SUPPORT)
	hvdcp20_dp_gpio = of_get_named_gpio(parent_np, "hvdcp20_dp_gpio", 0);
        if (hvdcp20_dp_gpio < 0) {
			hvdcp20_dp_gpio = U32_MAX;
			dev_info(chip->dev, "%s hvdcp20_dp_gpio=%d get fail\n", __func__,hvdcp20_dp_gpio);
        }
		if(hvdcp20_dp_gpio != U32_MAX){
			ret = gpio_request(hvdcp20_dp_gpio, "hvdcp20_dp_gpio");
			if (ret < 0){
				dev_info(chip->dev, "hvdcp20_dp_gpio request failed!\n");
			}
        }
#endif		
#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT)
		tc30_dm_gpio = of_get_named_gpio(parent_np, "tc30_dm_gpio", 0);
        if (tc30_dm_gpio < 0) {
			tc30_dm_gpio = U32_MAX;
			dev_info(chip->dev, "%s tc30_dm_gpio=%d get fail\n", __func__,tc30_dm_gpio);
        }
		if(tc30_dm_gpio != U32_MAX){
			ret = gpio_request(tc30_dm_gpio, "tc30_dm_gpio");
			if (ret < 0){
				dev_info(chip->dev, "tc30_dm_gpio request failed!\n");
			}
        }
#endif
	}

	if (chip->ceb_gpio != U32_MAX) {
		ret = devm_gpio_request_one(
				chip->dev, chip->ceb_gpio, GPIOF_DIR_OUT,
				devm_kasprintf(chip->dev, GFP_KERNEL,
				"rt9471_ceb_gpio.%s", dev_name(chip->dev)));
		if (ret < 0) {
			dev_notice(chip->dev, "%s gpio request fail(%d)\n",
					      __func__, ret);
			return ret;
		}
	}

	/* Register map */
	ret = of_property_read_u8(np, "rm-dev-addr", &desc->rm_dev_addr);
	if (ret < 0)
		dev_info(chip->dev, "%s no rm-dev-addr(%d)\n", __func__, ret);
	ret = of_property_read_string(np, "rm-name", &desc->rm_name);
	if (ret < 0)
		dev_info(chip->dev, "%s no rm-name(%d)\n", __func__, ret);

	/* Charger parameter */
	ret = of_property_read_u32(np, "vac_ovp", &desc->vac_ovp);
	if (ret < 0)
		dev_info(chip->dev, "%s no vac_ovp(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "mivr", &desc->mivr);
	if (ret < 0)
		dev_info(chip->dev, "%s no mivr(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "aicr", &desc->aicr);
	if (ret < 0)
		dev_info(chip->dev, "%s no aicr(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "cv", &desc->cv);
	if (ret < 0)
		dev_info(chip->dev, "%s no cv(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "ichg", &desc->ichg);
	if (ret < 0)
		dev_info(chip->dev, "%s no ichg(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "ieoc", &desc->ieoc) < 0;
	if (ret < 0)
		dev_info(chip->dev, "%s no ieoc(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "safe_tmr", &desc->safe_tmr);
	if (ret < 0)
		dev_info(chip->dev, "%s no safe_tmr(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "wdt", &desc->wdt);
	if (ret < 0)
		dev_info(chip->dev, "%s no wdt(%d)\n", __func__, ret);

	ret = of_property_read_u32(np, "mivr_track", &desc->mivr_track);
	if (ret < 0)
		dev_info(chip->dev, "%s no mivr_track(%d)\n", __func__, ret);
	if (desc->mivr_track >= RT9471_MIVRTRACK_MAX)
		desc->mivr_track = RT9471_MIVRTRACK_VBAT_300MV;
	ret = of_property_read_u32(np, "bc12_sel", &desc->bc12_sel);
	if (ret < 0)
		dev_info(chip->dev, "%s no bc12_sel(%d)\n", __func__, ret);
	dev_info(chip->dev, "%s bc12_sel %d\n", __func__, desc->bc12_sel);

	desc->en_safe_tmr = of_property_read_bool(np, "en_safe_tmr");
	desc->en_te = of_property_read_bool(np, "en_te");
	desc->en_jeita = of_property_read_bool(np, "en_jeita");
	desc->ceb_invert = of_property_read_bool(np, "ceb_invert");
	desc->dis_i2c_tout = of_property_read_bool(np, "dis_i2c_tout");
	desc->en_qon_rst = of_property_read_bool(np, "en_qon_rst");
	desc->auto_aicr = of_property_read_bool(np, "auto_aicr");

	memcpy(chip->irq_mask, rt9471_irq_maskall, RT9471_IRQIDX_MAX);
	while (true) {
		ret = of_property_read_string_index(np, "interrupt-names",
						    irqcnt, &name);
		if (ret < 0)
			break;
		irqcnt++;
		irqnum = rt9471_get_irq_number(chip, name);
		if (irqnum >= 0)
			rt9471_irq_unmask(chip, irqnum);
	}

	return 0;
}

static int rt9471_check_chg(struct rt9471_chip *chip)
{
	int ret = 0;
	u8 regval = 0;

	dev_info(chip->dev, "%s\n", __func__);

	ret = regmap_raw_read(chip->rm_dev,
			RT9471_REG_STAT0, &regval, sizeof(regval));
	if (ret < 0) {
		dev_notice(chip->dev, "%s: (%d) read fail\n", __func__, ret);
		return ret;
	}
	if (regval & RT9471_ST_CHGDONE_MASK)
		rt9471_chg_done_irq_handler(chip);
	else if (regval & RT9471_ST_CHGRDY_MASK)
		rt9471_chg_rdy_irq_handler(chip);

	return ret;
}

static int rt9471_sw_workaround(struct rt9471_chip *chip)
{
	int ret = 0;
	u32 regval = 0;

	dev_info(chip->dev, "%s\n", __func__);

	ret = rt9471_enable_hidden_mode(chip, true);
	if (ret < 0)
		return ret;

	ret = regmap_field_read(chip->rm_field[F_CHIP_REV], &regval);
	if (ret < 0)
		goto out;

	chip->chip_rev = regval;
	dev_info(chip->dev, "%s chip_rev = %d\n", __func__, chip->chip_rev);

	/* OTG load transient improvement */
	if (chip->chip_rev <= 3)
		ret = regmap_field_write(chip->rm_field[F_OTG_RES_COMP], 0x01);

out:
	rt9471_enable_hidden_mode(chip, false);
	return ret;
}

static int rt9471_init_setting(struct rt9471_chip *chip)
{
	int ret = 0;
	struct rt9471_desc *desc = chip->desc;
	u8 evt[RT9471_IRQIDX_MAX] = {0};

	dev_info(chip->dev, "%s\n", __func__);

	/* Mask all IRQs */
	ret = regmap_bulk_write(chip->rm_dev, RT9471_REG_MASK0,
			rt9471_irq_maskall, ARRAY_SIZE(rt9471_irq_maskall));
	if (ret < 0)
		dev_notice(chip->dev, "%s mask irq fail(%d)\n", __func__, ret);

	/* Clear all IRQs */
	ret = regmap_bulk_read(chip->rm_dev,
			RT9471_REG_IRQ0, evt, RT9471_IRQIDX_MAX);
	if (ret < 0)
		dev_notice(chip->dev, "%s clear irq fail(%d)\n", __func__, ret);

	ret = __rt9471_set_vac_ovp(chip, desc->vac_ovp);
	if (ret < 0)
		dev_notice(chip->dev, "%s set vac ovp fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_set_mivr(chip, desc->mivr);
	if (ret < 0)
		dev_notice(chip->dev, "%s set mivr fail(%d)\n", __func__, ret);

	ret = __rt9471_set_aicr(chip, desc->aicr);
	if (ret < 0)
		dev_notice(chip->dev, "%s set aicr fail(%d)\n", __func__, ret);

	ret = __rt9471_set_cv(chip, desc->cv);
	if (ret < 0)
		dev_notice(chip->dev, "%s set cv fail(%d)\n", __func__, ret);

	ret = __rt9471_set_ichg(chip, desc->ichg);
	if (ret < 0)
		dev_notice(chip->dev, "%s set ichg fail(%d)\n", __func__, ret);

	ret = __rt9471_set_ieoc(chip, desc->ieoc);
	if (ret < 0)
		dev_notice(chip->dev, "%s set ieoc fail(%d)\n", __func__, ret);

	ret = __rt9471_reset_eoc_state(chip);
	if (ret < 0)
		dev_notice(chip->dev, "%s reset eoc state fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_set_safe_tmr(chip, desc->safe_tmr);
	if (ret < 0)
		dev_notice(chip->dev, "%s set safe tmr fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_set_mivrtrack(chip, desc->mivr_track);
	if (ret < 0)
		dev_notice(chip->dev, "%s set mivrtrack fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_enable_safe_tmr(chip, desc->en_safe_tmr);
	if (ret < 0)
		dev_notice(chip->dev, "%s en safe tmr fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_enable_te(chip, desc->en_te);
	if (ret < 0)
		dev_notice(chip->dev, "%s en te fail(%d)\n", __func__, ret);

	ret = __rt9471_enable_jeita(chip, desc->en_jeita);
	if (ret < 0)
		dev_notice(chip->dev, "%s en jeita fail(%d)\n", __func__, ret);

	ret = __rt9471_disable_i2c_tout(chip, desc->dis_i2c_tout);
	if (ret < 0)
		dev_notice(chip->dev, "%s dis i2c tout fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_enable_qon_rst(chip, desc->en_qon_rst);
	if (ret < 0)
		dev_notice(chip->dev, "%s en qon rst fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_enable_autoaicr(chip, desc->auto_aicr);
	if (ret < 0)
		dev_notice(chip->dev, "%s en autoaicr fail(%d)\n",
				      __func__, ret);

	ret = rt9471_sw_workaround(chip);
	if (ret < 0)
		dev_notice(chip->dev, "%s sw workaround fail(%d)\n",
				      __func__, ret);

	ret = __rt9471_enable_bc12(chip, false);
	if (ret < 0)
		dev_notice(chip->dev, "%s dis bc12 fail(%d)\n", __func__, ret);

	ret = __rt9471_enable_dp_0_6v(chip, false);
	if (ret < 0){
		dev_notice(chip->dev,"%s __rt9471_enable_dp_0_6v(%d)\n", __func__, ret);
	}

#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT) || IS_ENABLED(CONFIG_MTK_HVDCP20_SUPPORT)
	ret = gpio_direction_output(hvdcp20_dp_gpio, 0);
	if (ret < 0) {
		dev_notice(chip->dev,"%s: set hvdcp20_dp_gpio fail\n", __func__);
	}
#endif
#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT)
	ret = gpio_direction_output(tc30_dm_gpio, 0);
	if (ret < 0) {
		dev_notice(chip->dev,"%s: set tc30_dm_gpio fail\n", __func__);
}
#endif

	/*
	 * Customization for MTK platform
	 * Primary charger: HZ controlled by sink vbus with TCPC enabled,
	 *		    CHG_EN controlled by charging algorithm
	 * Secondary charger: HZ=0 and CHG_EN=1 at needed,
	 *		      e.x.: PE10, PE20, etc...
	 */
	if (!chip->is_primary) {
		ret = rt9471_enable_hz(chip, true, RT9471_HZU_PP);
		if (ret < 0)
			dev_notice(chip->dev, "%s en hz fail(%d)\n",
					      __func__, ret);
		ret = __rt9471_enable_chg(chip, false);
		if (ret < 0)
			dev_notice(chip->dev, "%s dis chg fail(%d)\n",
					      __func__, ret);
		chip->enter_shipping_mode = true;
	}

	return 0;
}

static int rt9471_reset_register(struct rt9471_chip *chip)
{
	int ret = 0;

	dev_info(chip->dev, "%s\n", __func__);

	mutex_lock(&chip->hz_lock);
	chip->hz_users[RT9471_HZU_PP] = false;
	chip->hz_users[RT9471_HZU_BC12] = false;
	chip->hz_users[RT9471_HZU_OTG] = true;
	chip->hz_users[RT9471_HZU_VBUS_GD] = true;
	ret = regmap_field_write(chip->rm_field[F_REGRST], 0x01);
	mutex_unlock(&chip->hz_lock);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: set regrst fail\n", __func__);
		return ret;
	}

	ret = __rt9471_enable_autoaicr(chip, false);
	if (ret < 0)
		return ret;

	return __rt9471_set_wdt(chip, 0);
}

static bool rt9471_check_devinfo(struct rt9471_chip *chip)
{
	int ret = 0;

	ret = i2c_smbus_read_byte_data(chip->client, RT9471_REG_INFO);
	if (ret < 0) {
		dev_notice(chip->dev, "%s get devinfo fail(%d)\n",
				      __func__, ret);
		return false;
	}
	chip->dev_id = (ret & RT9471_DEVID_MASK) >> RT9471_DEVID_SHIFT;
	switch (chip->dev_id) {
	case RT9470_DEVID:
	case RT9470D_DEVID:
	case RT9471_DEVID:
	case RT9471D_DEVID:
		break;
	default:
		dev_notice(chip->dev, "%s incorrect devid 0x%02X\n",
				      __func__, chip->dev_id);
		return false;
	}
	chip->dev_rev = (ret & RT9471_DEVREV_MASK) >> RT9471_DEVREV_SHIFT;
	dev_info(chip->dev, "%s id = 0x%02X, rev = 0x%02X\n",
			    __func__, chip->dev_id, chip->dev_rev);

	return true;
}

static int rt9471_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s\n", __func__);

	ret = __rt9471_set_wdt(chip, chip->desc->wdt);
	if (ret < 0)
		dev_notice(chip->dev, "%s set wdt fail(%d)\n", __func__, ret);

	return ret;
}

static int rt9471_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s\n", __func__);

	ret = __rt9471_enable_chg(chip, false);
	if (ret < 0) {
		dev_notice(chip->dev, "%s en chg fail(%d)\n", __func__, ret);
		return ret;
	}

	ret = __rt9471_set_wdt(chip, 0);
	if (ret < 0)
		dev_notice(chip->dev, "%s set wdt fail(%d)\n", __func__, ret);

#if IS_ENABLED(CONFIG_MTK_HVDCP20_SUPPORT)
	rt9471_hvdcp20_set_dp_dm_recover(chg_dev);
#endif
#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT)
	rt9471_tc30_gpio_enable(chg_dev,false);
#endif

	return ret;
}

static int rt9471_enable_charging(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	ret = __rt9471_enable_chg(chip, en);
	if (ret < 0)
		dev_notice(chip->dev, "%s en chg fail(%d)\n", __func__, ret);

	return ret;
}

static int rt9471_is_charging_enabled(struct charger_device *chg_dev, bool *en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	return __rt9471_is_chg_enabled(chip, en);
}

static int rt9471_is_charging_done(struct charger_device *chg_dev, bool *done)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};

	ret = power_supply_get_property(chip->psy,
					POWER_SUPPLY_PROP_STATUS, &val);
	if (ret < 0)
		return ret;
	*done = (val.intval == POWER_SUPPLY_STATUS_FULL);

	return ret;
}

static int rt9471_get_mivr(struct charger_device *chg_dev, u32 *uV)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};
	int ret = 0;

	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &val);
	if (ret < 0)
		return ret;
	*uV = val.intval;
	return ret;
}

static int rt9471_set_mivr(struct charger_device *chg_dev, u32 uV)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = uV};

	return  power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &val);
}

static int rt9471_get_mivr_state(struct charger_device *chg_dev, bool *in_loop)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;
	u32 regval = 0;

	ret = regmap_field_read(chip->rm_field[F_ST_MIVR], &regval);
	if (ret < 0) {
		dev_notice(chip->dev, "%s:(%d) read mivr_state fail\n",
							__func__, ret);
		return ret;
	}
	*in_loop = regval;
	return ret;
}

static int rt9471_get_aicr(struct charger_device *chg_dev, u32 *uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};
	int ret = 0;

	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret < 0)
		return ret;
	*uA = val.intval;
	return ret;
}

static int rt9471_set_aicr(struct charger_device *chg_dev, u32 uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = uA};

	return power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
}

static int rt9471_get_min_aicr(struct charger_device *chg_dev, u32 *uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	*uA = chip->rm_range[F_IAICR]->linear_range_table->min;
	return 0;
}

static int rt9471_get_cv(struct charger_device *chg_dev, u32 *uV)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};
	int ret = 0;

	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
	if (ret < 0)
		return ret;
	*uV = val.intval;
	return ret;
}

static int rt9471_set_cv(struct charger_device *chg_dev, u32 uV)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = uV};

	return power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE, &val);
}

static int rt9471_get_ichg(struct charger_device *chg_dev, u32 *uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};
	int ret = 0;

	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
	if (ret < 0)
		return ret;
	*uA = val.intval;
	return ret;
}

static int rt9471_set_ichg(struct charger_device *chg_dev, u32 uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = uA};

	return power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
}

static int rt9471_get_min_ichg(struct charger_device *chg_dev, u32 *uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	*uA = chip->rm_range[F_CC]->linear_range_table->min;
	return 0;
}

static int rt9471_get_ieoc(struct charger_device *chg_dev, u32 *uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};
	int ret = 0;

	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, &val);
	if (ret < 0)
		return ret;
	*uA = val.intval;
	return ret;
}

static int rt9471_set_ieoc(struct charger_device *chg_dev, u32 uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = uA};

	return power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT, &val);
}

static int rt9471_reset_eoc_state(struct charger_device *chg_dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	return __rt9471_reset_eoc_state(chip);
}

static int rt9471_enable_te(struct charger_device *chg_dev, bool en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	return __rt9471_enable_te(chip, en);
}

static int rt9471_kick_wdt(struct charger_device *chg_dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	return __rt9471_kick_wdt(chip);
}

static int rt9471_event(struct charger_device *chg_dev, u32 event, u32 args)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s event = %d\n", __func__, event);

	power_supply_changed(chip->psy);

	return 0;
}

static int rt9471_enable_powerpath(struct charger_device *chg_dev, bool en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	return rt9471_enable_hz(chip, !en, RT9471_HZU_PP);
}

static int rt9471_is_powerpath_enabled(struct charger_device *chg_dev, bool *en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	mutex_lock(&chip->hz_lock);
	*en = !chip->hz_users[RT9471_HZU_PP];
	mutex_unlock(&chip->hz_lock);

	return 0;
}

static int rt9471_enable_safety_timer(struct charger_device *chg_dev, bool en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	return __rt9471_enable_safe_tmr(chip, en);
}

static int rt9471_is_safety_timer_enabled(struct charger_device *chg_dev,
					  bool *en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	u32 regval = 0;
	int ret = 0;

	ret = regmap_field_read(chip->rm_field[F_CHG_TMR_EN], &regval);
	if (ret < 0) {
		dev_notice(chip->dev, "%s: read safety_timer en fail(%d)\n",
						__func__, ret);
		return ret;
	}
	*en = regval;
	return ret;
}

static int rt9471_enable_otg(struct charger_device *chg_dev, bool en)
{
	bool is_en = false;
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	ret = rt9471_is_otg_enabled(chip,&is_en);
	if(ret < 0){
		dev_err(chip->dev, "rt9471_enable_otg error!\n");
		return ret;
	}
	dev_info(chip->dev, "%s: en = %d is_en = %d\n", __func__, en,is_en);
	if((en && !is_en) || (!en && is_en))
		ret = __rt9471_enable_otg(chip, en);
	else
		ret = -EINVAL;
	return ret;
}

static int rt9471_enable_discharge(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s en = %d\n", __func__, en);

	ret = rt9471_enable_hidden_mode(chip, true);
	if (ret < 0)
		return ret;

	ret = regmap_field_write(chip->rm_field[F_FORCE_EN_VBUS_SINK], en);
	if (ret < 0)
		dev_notice(chip->dev, "%s en = %d fail(%d)\n",
				      __func__, en, ret);

	rt9471_enable_hidden_mode(chip, false);

	return ret;
}

static int rt9471_set_boost_current_limit(struct charger_device *chg_dev,
					  u32 uA)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	int ret = 0;

	ret = regulator_set_current_limit(chip->regulator, uA, uA);
	return ret;
}

static int rt9471_set_boost_voltage_limit(struct charger_device *chg_dev,
					  u32 uV)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s uV= %d\n", __func__, uV);

	return rt9471_closest_reg_set(chip, F_OTG_CV, uV);
}

enum attach_type {
        ATTACH_TYPE_NONE,
        ATTACH_TYPE_PWR_RDY,
        ATTACH_TYPE_TYPEC,
        ATTACH_TYPE_PD,
        ATTACH_TYPE_PD_SDP,
        ATTACH_TYPE_PD_DCP,
        ATTACH_TYPE_PD_NONSTD,
};

static int rt9471_enable_chg_type_det(struct charger_device *chg_dev, bool en)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	if(chip == NULL || atomic_read(&chip->is_shutdown)) {
		ret = -EINVAL;
		goto out;
	}
	rt9471_is_otg_enabled(chip,&chip->rt9471_otg_enable_flag);
	dev_info(chip->dev, "%s en = %d attach = %d otg_en_flag = %d\n", __func__, en,chip->attach,chip->rt9471_otg_enable_flag);
	if (!en)
	{
		complete(&chip->aicc_done);
		complete(&chip->pe_done);
		chip->bc12_done = false;
		goto bc12_entry;
	}
	else if(chip->attach)
		chip->attach = false;
	if(atomic_read(&chip->vbus_gd) && !chip->bc12_done)
		goto out;
bc12_entry:
	if (chip->dev_id != RT9470D_DEVID && chip->dev_id != RT9471D_DEVID) {
		dev_notice(chip->dev, "%s bc12 not supported\n", __func__);
		goto out;
	}
	if (chip->desc->bc12_sel)
		goto out;
	if(chip->rt9471_otg_enable_flag)
		goto out;
	mutex_lock(&chip->bc12_lock);
	atomic_set(&chip->vbus_gd, en);
	rt9471_enable_hz(chip, !en, RT9471_HZU_BC12);
	ret = (en ? rt9471_bc12_preprocess : rt9471_bc12_postprocess)(chip);
	mutex_unlock(&chip->bc12_lock);
	if (ret < 0)
		dev_notice(chip->dev, "%s en bc12 fail(%d)\n", __func__, ret);
out:
	return ret;
}

static int rt9471_dump_registers(struct charger_device *chg_dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	return __rt9471_dump_registers(chip);
}

static int rt9471_run_aicc(struct charger_device *chg_dev, u32 *uA)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	union power_supply_propval val = {.intval = 0};
	u32 aicr = 0, aicc = 0, chg_mivr;

	dev_info(chip->dev, "%s chip_rev = %d\n", __func__, chip->chip_rev);
	if (chip->chip_rev < 4)
		return -EOPNOTSUPP;

	ret = regmap_field_read(chip->rm_field[F_ST_MIVR], &chg_mivr);
	if (ret < 0)
		return ret;
	if (!chg_mivr) {
		dev_info(chip->dev, "%s mivr stat not act\n", __func__);
		return ret;
	}

	/* Backup the aicr */
	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret < 0)
		return ret;
	aicr = val.intval;

	disable_irq(chip->irq);

	/* Start aicc */
	ret = regmap_field_write(chip->rm_field[F_AICC_EN], 0x01);
	if (ret < 0) {
		enable_irq(chip->irq);
		dev_notice(chip->dev, "%s aicc en fail(%d)\n", __func__, ret);
		goto out;
	}
	if (!rt9471_is_vbus_gd(chip)) {
		enable_irq(chip->irq);
		ret = -EPERM;
		goto out;
	}
	reinit_completion(&chip->aicc_done);

	enable_irq(chip->irq);

	ret = wait_for_completion_timeout(&chip->aicc_done,
					  msecs_to_jiffies(1000));
	if (ret == 0) {
		dev_notice(chip->dev, "%s wait aicc timeout\n", __func__);
		ret = -ETIMEDOUT;
		goto out;
	}
	if (!rt9471_is_vbus_gd(chip)) {
		ret = -EPERM;
		goto out;
	}

	/* Get the aicc result */
	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret < 0)
		goto out;
	aicc = val.intval;
	dev_info(chip->dev, "%s aicc = %d\n", __func__, aicc);
	*uA = aicc;
out:
	ret = regmap_field_write(chip->rm_field[F_AICC_EN], 0x00);
	/* Restore the aicr */
	val.intval = aicr;
	return  power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
}

static int rt9471_enable_pump_express(struct rt9471_chip *chip, bool pe20)
{
	int ret = 0;
	const unsigned int ms = pe20 ? 1400 : 2800;
	struct rt9471_desc *desc = chip->desc;
	union power_supply_propval val = {.intval = 0};

	dev_info(chip->dev, "%s pe20 = %d\n", __func__, pe20);

	ret = regmap_field_write(chip->rm_field[F_PE_SEL], pe20 ? 0x01 : 0x00);
	if (ret < 0)
		return ret;
	/* Set MIVR/AICR/ICHG/CHG_EN */
	val.intval = desc->mivr;
	ret =  power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT, &val);
	if (ret < 0)
		return ret;
	val.intval = desc->aicr;
	ret = power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret < 0)
		return ret;
	val.intval = desc->ichg;
	ret = power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT, &val);
	if (ret < 0)
		return ret;
	ret = __rt9471_enable_chg(chip, true);
	if (ret < 0)
		return ret;

	disable_irq(chip->irq);

	/* Start pump express */
	ret = regmap_field_write(chip->rm_field[F_PE_EN], 0x01);
	if (ret < 0) {
		enable_irq(chip->irq);
		dev_notice(chip->dev, "%s pe en fail(%d)\n", __func__, ret);
		goto out;
	}
	if (!rt9471_is_vbus_gd(chip)) {
		enable_irq(chip->irq);
		ret = -EPERM;
		goto out;
	}
	reinit_completion(&chip->pe_done);

	enable_irq(chip->irq);

	ret = wait_for_completion_timeout(&chip->pe_done, msecs_to_jiffies(ms));
	if (ret == 0) {
		dev_notice(chip->dev, "%s wait pe timeout\n", __func__);
		ret = -ETIMEDOUT;
		goto out;
	}
	if (!rt9471_is_vbus_gd(chip)) {
		ret = -EPERM;
		goto out;
	}
	ret = 0;
out:
	ret = regmap_field_write(chip->rm_field[F_PE_EN], 0x00);
	return ret;
}

static int rt9471_send_ta_current_pattern(struct charger_device *chg_dev,
					  bool is_inc)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s is_inc = %d, chip_rev = %d\n",
			    __func__, is_inc, chip->chip_rev);
	if (chip->chip_rev < 4)
		return -EOPNOTSUPP;

	ret = regmap_field_write(chip->rm_field[F_PE10_INC], is_inc?0x01:0x00);
	if (ret < 0)
		return ret;

	return rt9471_enable_pump_express(chip, false);
}

static int rt9471_get_boost_status(struct charger_device *chg_dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	int ret=0;
	enum rt9471_ic_stat ic_stat = RT9471_ICSTAT_SLEEP;

	ret = __rt9471_get_ic_stat(chip, &ic_stat);
	if (ret < 0)
		return ret;
	ret = (ic_stat == RT9471_ICSTAT_OTG);

	dev_info(chip->dev, "%s ic_stat=%d\n", __func__,ret);

	return ret;
}

static int rt9471_send_ta20_current_pattern(struct charger_device *chg_dev,
					    u32 uV)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s target = %d, chip_rev = %d\n",
			    __func__, uV, chip->chip_rev);
	if (chip->chip_rev < 4)
		return -EOPNOTSUPP;

	ret = rt9471_closest_reg_set(chip, F_PE20_CODE, uV);
	if (ret < 0)
		return ret;

	return rt9471_enable_pump_express(chip, true);
}

static int rt9471_reset_ta(struct charger_device *chg_dev)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	struct rt9471_desc *desc = chip->desc;
	union power_supply_propval val = {.intval = 0};
	u32 aicr = 0;

	dev_info(chip->dev, "%s chip_rev = %d\n", __func__, chip->chip_rev);
	if (chip->chip_rev < 4)
		return -EOPNOTSUPP;

	if (desc->auto_aicr) {
		ret = __rt9471_enable_autoaicr(chip, false);
		if (ret < 0)
			goto out;
	}
	/* Backup the aicr */
	ret = power_supply_get_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret < 0)
		goto out;
	aicr = val.intval;

	/* 50mA */
	val.intval = 50000;
	ret = power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (ret < 0)
		goto out_restore_aicr;
	mdelay(250);
out_restore_aicr:
	/* Restore the aicr */
	val.intval = aicr;
	ret = power_supply_set_property(chip->psy,
			POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
out:
	if (desc->auto_aicr)
		__rt9471_enable_autoaicr(chip, true);
	return ret;
}

static int rt9471_set_pe20_efficiency_table(struct charger_device *chg_dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s chip_rev = %d\n", __func__, chip->chip_rev);

	return -EOPNOTSUPP;
}

static int rt9471_enable_cable_drop_comp(struct charger_device *chg_dev,
					 bool en)
{
	int ret = 0;
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s en = %d, chip_rev = %d\n",
			    __func__, en, chip->chip_rev);
	if (chip->chip_rev < 4)
		return -EOPNOTSUPP;

	if (en)
		return ret;

	ret = rt9471_closest_reg_set(chip,
			F_PE20_CODE, RT9471_PE20_CODE_IR_COMPEN_MASK);
	if (ret < 0)
		return ret;

	return rt9471_enable_pump_express(chip, true);
}

static int rt9471_enable_hz_ext(struct charger_device *chg_dev, bool en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);

	dev_info(chip->dev, "%s: en = %d\n", __func__, en);

	return __rt9471_enable_hz(chip,en);
}

static int rt9471_pfm_enable(struct charger_device *chg_dev, bool en)
{
	struct rt9471_chip *chip = dev_get_drvdata(&chg_dev->dev);
	const u8 mask = BIT(3);

	dev_info(chip->dev, "%s: en = %d\n", __func__, en);
	return regmap_update_bits(chip->rm_dev,
					RT9471_REG_FUNCTION, mask, en ? mask : 0<<3);
}

static struct charger_ops rt9471_chg_ops = {
	/* cable plug in/out for primary charger */
	.plug_in = rt9471_plug_in,
	.plug_out = rt9471_plug_out,

	/* enable/disable charger */
	.enable = rt9471_enable_charging,
	.is_enabled = rt9471_is_charging_enabled,
	.is_charging_done = rt9471_is_charging_done,

	/* get/set minimun input voltage regulation */
	.get_mivr = rt9471_get_mivr,
	.set_mivr = rt9471_set_mivr,
	.get_mivr_state = rt9471_get_mivr_state,

	/* get/set input current */
	.get_input_current = rt9471_get_aicr,
	.set_input_current = rt9471_set_aicr,
	.get_min_input_current = rt9471_get_min_aicr,

	/* get/set charging voltage */
	.get_constant_voltage = rt9471_get_cv,
	.set_constant_voltage = rt9471_set_cv,

	/* get/set charging current*/
	.get_charging_current = rt9471_get_ichg,
	.set_charging_current = rt9471_set_ichg,
	.get_min_charging_current = rt9471_get_min_ichg,

	/* get/set termination current */
	.get_eoc_current = rt9471_get_ieoc,
	.set_eoc_current = rt9471_set_ieoc,
	.reset_eoc_state = rt9471_reset_eoc_state,

	/* enable te */
	.enable_termination = rt9471_enable_te,

	/* kick wdt */
	.kick_wdt = rt9471_kick_wdt,

	.event = rt9471_event,

	/* enable/disable powerpath for primary charger */
	.enable_powerpath = rt9471_enable_powerpath,
	.is_powerpath_enabled = rt9471_is_powerpath_enabled,

	/* enable/disable chip for secondary charger */
	.enable_chip = rt9471_enable_powerpath,
	.is_chip_enabled = rt9471_is_powerpath_enabled,

	/* enable/disable charging safety timer */
	.enable_safety_timer = rt9471_enable_safety_timer,
	.is_safety_timer_enabled = rt9471_is_safety_timer_enabled,

	/* OTG */
	.enable_otg = rt9471_enable_otg,
	.enable_discharge = rt9471_enable_discharge,
	.set_boost_current_limit = rt9471_set_boost_current_limit,

	.set_boost_voltage = rt9471_set_boost_voltage_limit,

	/* charger type detection */
	.enable_chg_type_det = rt9471_enable_chg_type_det,

	.dump_registers = rt9471_dump_registers,

	/* new features for chip_rev >= 4, AICC */
	.run_aicl = rt9471_run_aicc,
	/* new features for chip_rev >= 4, PE+/PE+2.0 */
	.send_ta_current_pattern = rt9471_send_ta_current_pattern,
	.send_ta20_current_pattern = rt9471_send_ta20_current_pattern,
	.reset_ta = rt9471_reset_ta,
	.set_pe20_efficiency_table = rt9471_set_pe20_efficiency_table,
	.enable_cable_drop_comp = rt9471_enable_cable_drop_comp,
#if IS_ENABLED(CONFIG_MTK_HVDCP20_SUPPORT)
	.set_dm_0_6_v = rt9471_hvdcp20_set_dm_0_6_v,
	.set_dp_0_6_v = rt9471_hvdcp20_set_dp_0_6_v,
	.set_dp_3_0_v = rt9471_hvdcp20_set_dp_3_0_v,
	.set_dp_dm_recover = rt9471_hvdcp20_set_dp_dm_recover,
#endif
#if IS_ENABLED(CONFIG_MTK_TC30_SUPPORT)
	.set_tc30_gpio_enable = rt9471_tc30_gpio_enable,
#endif
	.enable_hz = rt9471_enable_hz_ext,
	.get_boost_status = rt9471_get_boost_status,
	.set_pfm_mode = rt9471_pfm_enable,
};

static int rt9471_init_chg(struct rt9471_chip *chip)
{
	chip->chg_dev = charger_device_register(chip->desc->chg_name,
			chip->dev, chip, &rt9471_chg_ops, &chip->chg_props);
	if (!chip->chg_dev->dev.class || IS_ERR_OR_NULL(chip->chg_dev))
		return -EPROBE_DEFER;

	return 0;
}

static ssize_t shipping_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	int ret = 0, tmp = 0;
	struct rt9471_chip *chip = dev_get_drvdata(dev);

	ret = kstrtoint(buf, 10, &tmp);
	if (ret < 0) {
		dev_notice(dev, "%s parsing number fail(%d)\n", __func__, ret);
		return -EINVAL;
	}
	if (tmp != 5526789)
		return -EINVAL;
	chip->enter_shipping_mode = true;
	/*
	 * Use kernel_halt() instead of kernel_power_off() to prevent
	 * the system from booting again while cable still plugged-in.
	 */
	/* kernel_halt(); is not in GKI ABI list */
	kernel_power_off();

	return count;
}

static const DEVICE_ATTR_WO(shipping_mode);

/* regmap init */
static const struct regmap_config rt9471_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RT9471_REG_MAX,
	.cache_type = REGCACHE_NONE,
};

static int rt9471_init_regmap(struct rt9471_chip *chip)
{
	int i = 0;
	struct regmap_field *rf;

	dev_info(chip->dev, "%s\n", __func__);

	chip->rm_dev = devm_regmap_init_i2c(chip->client,
					    &rt9471_regmap_config);
	if (IS_ERR(chip->rm_dev)) {
		dev_notice(chip->dev, "%s fail(%ld)\n",
				      __func__, PTR_ERR(chip->rm_dev));
		return -EIO;
	}

	for (i = 0; i < ARRAY_SIZE(rt9471_reg_fields); i++) {
		rf = devm_regmap_field_alloc(chip->dev, chip->rm_dev,
					     rt9471_reg_fields[i]);
		if (IS_ERR(rf)) {
			dev_notice(chip->dev, "Failed to alloc regmap field\n");
			return PTR_ERR(rf);
		}
		chip->rm_field[i] = rf;
	}

	for (i = 0; i < ARRAY_SIZE(rt9471_rm_ranges); i++)
		chip->rm_range[rt9471_rm_ranges[i].fd] = &rt9471_rm_ranges[i];

	return 0;
}

/*regulator otg ops*/
static int rt9471_set_current_limit(struct regulator_dev *rdev,
						int min_uA, int max_uA)
{
	struct rt9471_chip *chip = rdev_get_drvdata(rdev);
	int num = ARRAY_SIZE(rt9471_otgcc);

	if (min_uA < rt9471_otgcc[0])
		min_uA = rt9471_otgcc[0];
	else if (min_uA > rt9471_otgcc[num - 1])
		min_uA = rt9471_otgcc[num - 1];

	return __rt9471_set_otgcc(chip, min_uA);
}

static int rt9471_get_current_limit(struct regulator_dev *rdev)
{
	int ret = 0;
	struct rt9471_chip *chip = rdev_get_drvdata(rdev);

	ret = rt9471_closest_value(chip, F_OTG_CC);
	if (ret < 0)
		dev_notice(chip->dev, "%s: read otg_cc fail\n", __func__);

	return ret;

}

static const struct regulator_ops rt9471_chg_otg_ops = {
	.list_voltage = regulator_list_voltage_linear,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_current_limit = rt9471_set_current_limit,
	.get_current_limit = rt9471_get_current_limit,
};

static const struct regulator_desc rt9471_otg_rdesc = {
	.of_match = "usb-otg-vbus",
	.name = "usb-otg-vbus",
	.ops = &rt9471_chg_otg_ops,
	.owner = THIS_MODULE,
	.type = REGULATOR_VOLTAGE,
	.min_uV = 4850000,
	.uV_step = 150000, /* step  150mV */
	.n_voltages = 4, /* 4850mV to 5300mV */
	.vsel_reg = RT9471_REG_OTGCFG,
	.vsel_mask = RT9471_OTGCV_MASK,
	.enable_reg = RT9471_REG_FUNCTION,
	.enable_mask = RT9471_OTG_EN_MASK,
};

static const struct regulator_init_data rt9471_vbus_init_data = {
	.constraints = {
	.valid_ops_mask = REGULATOR_CHANGE_STATUS|REGULATOR_CHANGE_CURRENT,
	.min_uA = 500000,
	.max_uA = 1200000,
	},
};

static rt9471_init_regulator(struct rt9471_chip *chip)
{
	struct regulator_config config = { };

	dev_info(chip->dev, "%s\n", __func__);

	config.dev = chip->dev;
	config.driver_data = chip;
	config.init_data = &rt9471_vbus_init_data;
	chip->otg_rdev = devm_regulator_register(chip->dev, &rt9471_otg_rdesc,
						&config);
	if (IS_ERR(chip->otg_rdev))
		return PTR_ERR(chip->otg_rdev);
	chip->regulator = devm_regulator_get(chip->dev, "usb-otg-vbus");
	return IS_ERR(chip->regulator) ? PTR_ERR(chip->regulator) : 0;
}

/* ======================= */
/* rt9471 Power Supply Ops */
/* ======================= */
static int rt9471_charger_get_online(struct rt9471_chip *chip,
				     union power_supply_propval *val)
{
#if IS_ENABLED(CONFIG_WIRELESS_TA)
	struct power_supply *psy;
	union power_supply_propval prop;
	int ret=0;

	psy = power_supply_get_by_name("wireless");
	if (IS_ERR_OR_NULL(psy)) {
		dev_err(chip->dev,"%s: get wireless power supply failed\n", __func__);
		return -EINVAL;
	}

	ret = power_supply_get_property(psy,POWER_SUPPLY_PROP_ONLINE, &prop);
	if(prop.intval)
		val->intval=prop.intval;
	else
#endif
		val->intval = atomic_read(&chip->vbus_gd);

	dev_info(chip->dev, "%s: online = %d\n", __func__, val->intval);
	return 0;
}

static int rt9471_charger_set_online(struct rt9471_chip *chip,
				     const union power_supply_propval *val)
{
	if (val->intval == ATTACH_TYPE_PD_DCP && chip->bc12_done) {
		dev_info(chip->dev, "%s: ignore pd_dcp after bc12 done\n", __func__);
		return 0;
	}

	if (val->intval == ATTACH_TYPE_PD_SDP) {
		atomic_set(&chip->vbus_gd, true);
		chip->attach = !!val->intval;
		chip->psy_desc.type = POWER_SUPPLY_TYPE_USB;
		chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_SDP;
		power_supply_changed(chip->psy);
		dev_info(chip->dev, "%s: attach = %d,type = %d\n", __func__, val->intval,chip->psy_desc.type);
		return 0;
	}

	return rt9471_enable_chg_type_det(chip->chg_dev, val->intval);
}

static int rt9471_charger_get_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct rt9471_chip *chip = power_supply_get_drvdata(psy);
	enum rt9471_ic_stat ic_stat = RT9471_ICSTAT_MAX;
	int ret = 0;
	bool chg_en = false;
	dev_dbg(chip->dev, "%s: prop = %d\n", __func__, psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = RT9471_MANUFACTURER;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		ret = rt9471_charger_get_online(chip, val);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = chip->psy_desc.type;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = chip->psy_usb_type;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		__rt9471_is_chg_enabled(chip,&chg_en);
		ret = __rt9471_get_ic_stat(chip, &ic_stat);
		if (ret < 0)
			dev_info(chip->dev,
				"%s: get rt9471 ic_status failed\n", __func__);
		dev_info(chip->dev,"%s:ic_status=%d chg_en=%d\n", __func__,ic_stat,chg_en);
		switch (ic_stat) {
		case RT9471_ICSTAT_CHGFAULT:
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
			break;
		case RT9471_ICSTAT_TRICKLECHG:
		case RT9471_ICSTAT_PRECHG:
		case RT9471_ICSTAT_FASTCHG:
		case RT9471_ICSTAT_IEOC:
		case RT9471_ICSTAT_BGCHG:
		case RT9471_ICSTAT_VBUSRDY:
		case RT9471_ICSTAT_SLEEP:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case RT9471_ICSTAT_CHGDONE:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		case RT9471_ICSTAT_OTG:
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		default:
			ret = -ENODATA;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (chip->psy_desc.type == POWER_SUPPLY_TYPE_USB)
			val->intval = 500000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (chip->psy_desc.type == POWER_SUPPLY_TYPE_USB)
			val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = __rt9471_get_ichg(chip, &(val->intval));
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = __rt9471_get_cv(chip, &(val->intval));
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = __rt9471_get_aicr(chip, &(val->intval));
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = __rt9471_get_mivr(chip, &(val->intval));
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = __rt9471_get_ieoc(chip, &(val->intval));
		break;
	default:
		ret = -ENODATA;
		break;
	}
	return ret;
}

static int rt9471_charger_set_property(struct power_supply *psy,
				       enum power_supply_property psp,
				       const union power_supply_propval *val)
{
	struct rt9471_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	dev_dbg(chip->dev, "%s: prop = %d\n", __func__, psp);
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		ret = rt9471_charger_set_online(chip, val);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		ret = __rt9471_set_ichg(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE:
		ret = __rt9471_set_cv(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		ret = __rt9471_set_aicr(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT:
		ret = __rt9471_set_mivr(chip, val->intval);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		ret = __rt9471_set_ieoc(chip, val->intval);
		break;
#if IS_ENABLED(CONFIG_WIRELESS_TA)
	case POWER_SUPPLY_PROP_TYPE:
		chip->psy_desc.type=val->intval;
		dev_info(chip->dev,"%s:type=%d\n",__func__,chip->psy_desc.type);
		ret=0;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		chip->psy_usb_type=val->intval;
		dev_info(chip->dev,"%s:psy_usb_type=%d\n",__func__,chip->psy_usb_type);
		ret=0;
		break;
#endif
	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int rt9471_charger_property_is_writeable(struct power_supply *psy,
						enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property rt9471_charger_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
};

static enum power_supply_usb_type rt9471_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};

static const struct power_supply_desc rt9471_charger_desc = {
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= rt9471_charger_properties,
	.num_properties		= ARRAY_SIZE(rt9471_charger_properties),
	.get_property		= rt9471_charger_get_property,
	.set_property		= rt9471_charger_set_property,
	.property_is_writeable	= rt9471_charger_property_is_writeable,
	.usb_types		= rt9471_charger_usb_types,
	.num_usb_types		= ARRAY_SIZE(rt9471_charger_usb_types),
};

static char *rt9471_charger_supplied_to[] = {
	"battery",
	"mtk-master-charger",
	"bc12",
	"charger"
};

static int rt9471_init_psy(struct rt9471_chip *chip)
{
	struct power_supply_config charger_cfg = {};

	dev_info(chip->dev, "%s\n", __func__);

	chip->psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->psy_usb_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	/* power supply register */
	memcpy(&chip->psy_desc,
		&rt9471_charger_desc, sizeof(chip->psy_desc));
	chip->psy_desc.name = "charger";

	charger_cfg.drv_data = chip;
	charger_cfg.of_node = chip->dev->of_node;
	charger_cfg.supplied_to = rt9471_charger_supplied_to;
	charger_cfg.num_supplicants = ARRAY_SIZE(rt9471_charger_supplied_to);
	chip->psy = devm_power_supply_register(chip->dev,
					&chip->psy_desc, &charger_cfg);
	pr_err("[yuchen] %s:%d, rt9471 psy: %p\n", __func__, __LINE__, chip->psy);
	return IS_ERR_OR_NULL(chip->psy);
}

static int rt9471_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret = 0;
	struct rt9471_chip *chip = NULL;

	dev_info(&client->dev, "%s (%s)\n", __func__, RT9471_DRV_VERSION);

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);
	mutex_init(&chip->io_lock);
	mutex_init(&chip->bc12_lock);
	mutex_init(&chip->hidden_mode_lock);
	mutex_init(&chip->hz_lock);
	chip->rt9471_otg_enable_flag = false;
	chip->bc12_done = false;
	atomic_set(&chip->is_shutdown, false);
	chip->hidden_mode_cnt = 0;
	chip->chg_done_once = false;
	chip->buck_dwork_ws =
		wakeup_source_register(chip->dev,
				       devm_kasprintf(chip->dev, GFP_KERNEL,
				       "rt9471_buck_dwork_ws.%s",
				       dev_name(chip->dev)));
	INIT_DELAYED_WORK(&chip->buck_dwork, rt9471_buck_dwork_handler);
	chip->enter_shipping_mode = false;
	init_completion(&chip->aicc_done);
	init_completion(&chip->pe_done);
	chip->is_primary = false;

	if (!rt9471_check_devinfo(chip)) {
		ret = -ENODEV;
		goto err_nodev;
	}

	ret = rt9471_parse_dt(chip);
	if (ret < 0)
		dev_notice(chip->dev, "%s parse dt fail(%d)\n", __func__, ret);

	ret = rt9471_init_regmap(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s register regmap fail(%d)\n",
				      __func__, ret);
		goto err_register_rm;
	}

	ret = rt9471_reset_register(chip);
	if (ret < 0)
		dev_notice(chip->dev, "%s reset register fail(%d)\n",
				      __func__, ret);

	ret = rt9471_init_setting(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init fail(%d)\n", __func__, ret);
		goto err_init;
	}

	if ((chip->dev_id == RT9470D_DEVID || chip->dev_id == RT9471D_DEVID)
		&& (chip->desc->bc12_sel == 0)) {
		atomic_set(&chip->vbus_gd, false);
		chip->attach = false;
		chip->port = RT9471_PORTSTAT_NOINFO;
		atomic_set(&chip->bc12_en, -1);
		init_waitqueue_head(&chip->bc12_en_req);
		chip->bc12_en_ws =
			wakeup_source_register(chip->dev,
					       devm_kasprintf(chip->dev,
					       GFP_KERNEL,
					       "rt9471_bc12_en_ws.%s",
					       dev_name(chip->dev)));
		chip->bc12_en_kthread =
			kthread_run(rt9471_bc12_en_kthread, chip, "%s",
				    devm_kasprintf(chip->dev, GFP_KERNEL,
				    "rt9471_bc12_en_kthread.%s",
				    dev_name(chip->dev)));
		if (IS_ERR(chip->bc12_en_kthread)) {
			ret = PTR_ERR(chip->bc12_en_kthread);
			dev_notice(chip->dev, "%s kthread run fail(%d)\n",
					      __func__, ret);
			goto err_kthread_run;
		}
	}

	ret = rt9471_init_chg(chip);
	if (ret) {
		ret = -EPROBE_DEFER;
		dev_notice(chip->dev, "%s register chg dev fail(%d)\n",
				      __func__, ret);
		goto err_register_chg_dev;
	}

	ret = rt9471_register_irq(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s register irq fail(%d)\n",
				      __func__, ret);
		goto err_register_irq;
	}

	ret = rt9471_check_chg(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s check chg(%d)\n", __func__, ret);
		goto err_check_chg;
	}

	ret = rt9471_init_irq(chip);
	if (ret < 0) {
		dev_notice(chip->dev, "%s init irq fail(%d)\n", __func__, ret);
		goto err_init_irq;
	}

	ret = device_create_file(chip->dev, &dev_attr_shipping_mode);
	if (ret < 0) {
		dev_notice(chip->dev, "%s create file fail(%d)\n",
				      __func__, ret);
		goto err_create_file;
	}

	ret = rt9471_init_regulator(chip);
	if (ret) {
		ret = PTR_ERR(chip->otg_rdev);
		dev_notice(chip->dev, "%s regulator register fail\n", __func__);
		goto err_regulator_dev;
	}

	ret = rt9471_init_psy(chip);
	if (ret) {
		ret = PTR_ERR(chip->psy);
		dev_notice(chip->dev,
			"Fail to register power supply dev, is NULL = %d\n",
							(chip->psy == NULL));
		goto err_register_psy;
	}

	__rt9471_dump_registers(chip);
	dev_info(chip->dev, "%s successfully\n", __func__);
	return 0;

err_register_psy:
	if (!IS_ERR_OR_NULL(chip->psy))
		power_supply_put(chip->psy);
err_regulator_dev:
err_create_file:
	disable_irq(chip->irq);
err_init_irq:
err_check_chg:
err_register_irq:
	charger_device_unregister(chip->chg_dev);
err_register_chg_dev:
	if ((chip->dev_id == RT9470D_DEVID || chip->dev_id == RT9471D_DEVID)
		&& (chip->desc->bc12_sel == 0)) {
		kthread_stop(chip->bc12_en_kthread);
		wakeup_source_unregister(chip->bc12_en_ws);
	}
err_kthread_run:
	cancel_delayed_work_sync(&chip->buck_dwork);
err_init:
	rt9471_reset_register(chip);
err_register_rm:
err_nodev:
	mutex_destroy(&chip->io_lock);
	mutex_destroy(&chip->bc12_lock);
	mutex_destroy(&chip->hidden_mode_lock);
	mutex_destroy(&chip->hz_lock);
	wakeup_source_unregister(chip->buck_dwork_ws);

	return ret;
}

static int rt9471_remove(struct i2c_client *client)
{
	struct rt9471_chip *chip = i2c_get_clientdata(client);

	if (!chip)
		return 0;
	dev_info(chip->dev, "%s\n", __func__);

	device_remove_file(chip->dev, &dev_attr_shipping_mode);
	if (chip->psy)
		power_supply_put(chip->psy);
	disable_irq(chip->irq);
	charger_device_unregister(chip->chg_dev);
	if ((chip->dev_id == RT9470D_DEVID || chip->dev_id == RT9471D_DEVID)
		&& (chip->desc->bc12_sel == 0)) {
		kthread_stop(chip->bc12_en_kthread);
		wakeup_source_unregister(chip->bc12_en_ws);
	}
	cancel_delayed_work_sync(&chip->buck_dwork);
	rt9471_reset_register(chip);
	mutex_destroy(&chip->io_lock);
	mutex_destroy(&chip->bc12_lock);
	mutex_destroy(&chip->hidden_mode_lock);
	mutex_destroy(&chip->hz_lock);
	wakeup_source_unregister(chip->buck_dwork_ws);
	i2c_set_clientdata(client, NULL);
	return 0;
}

static void rt9471_shutdown(struct i2c_client *client)
{
	int ret = 0;
	struct rt9471_chip *chip = i2c_get_clientdata(client);

	if (!chip)
         return;

	dev_info(chip->dev, "%s\n", __func__);

	atomic_set(&chip->is_shutdown, true);
	disable_irq(chip->irq);
	charger_device_unregister(chip->chg_dev);
	if ((chip->dev_id == RT9470D_DEVID || chip->dev_id == RT9471D_DEVID)
		&& (chip->desc->bc12_sel == 0))
		kthread_stop(chip->bc12_en_kthread);
	cancel_delayed_work_sync(&chip->buck_dwork);
	rt9471_reset_register(chip);
	if (chip->enter_shipping_mode)
		ret = __rt9471_enable_shipmode(chip);
	if (ret < 0)
		dev_notice(chip->dev, "%s: (%d) enable shipmode fail\n",
								__func__, ret);
	i2c_set_clientdata(client, NULL);
}

static int rt9471_suspend(struct device *dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(dev);
 
	if(!chip)
		return 0;
 
	dev_info(dev, "%s\n", __func__);
	if (device_may_wakeup(dev))
		enable_irq_wake(chip->irq);
	disable_irq(chip->irq);

	return 0;
}

static int rt9471_resume(struct device *dev)
{
	struct rt9471_chip *chip = dev_get_drvdata(dev);

	if(!chip)
		return 0;

	dev_info(dev, "%s\n", __func__);
	enable_irq(chip->irq);
	if (device_may_wakeup(dev))
		disable_irq_wake(chip->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(rt9471_pm_ops, rt9471_suspend, rt9471_resume);

static const struct of_device_id rt9471_of_device_id[] = {
	{ .compatible = "richtek,rt9470", },
	{ .compatible = "richtek,rt9471", },
	{ .compatible = "richtek,swchg", },
	{ },
};
MODULE_DEVICE_TABLE(of, rt9471_of_device_id);

static const struct i2c_device_id rt9471_i2c_device_id[] = {
	{ "rt9470", 0 },
	{ "rt9471", 1 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rt9471_i2c_device_id);

static struct i2c_driver rt9471_i2c_driver = {
	.driver = {
		.name = "rt9471",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rt9471_of_device_id),
		.pm = &rt9471_pm_ops,
	},
	.probe = rt9471_probe,
	.remove = rt9471_remove,
	.shutdown = rt9471_shutdown,
	.id_table = rt9471_i2c_device_id,
};
module_i2c_driver(rt9471_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ShuFanLee <shufan_lee@richtek.com>");
MODULE_DESCRIPTION("RT9471 Charger Driver");
MODULE_VERSION(RT9471_DRV_VERSION);

/*
 * Release Note
 * 1.0.17
 * (1) Fix rt9471_is_powerpath_enabled()
 *
 * 1.0.16
 * (1) Rearrange the resources alloc and free in driver probing/removing
 *
 * 1.0.15
 * (1) Reset EOC state at initial setting
 * (2) Revise HZ usage
 * (3) Simplify BC12 flow
 * (4) CHG_EN=1 at plug_out
 *
 * 1.0.14
 * (1) Enable HZ before entering shipping mode
 * (2) Rearrange the resources alloc and free in driver probing/removing
 *
 * 1.0.13
 * (1) Do not wait for irqs of aicc_done and pe_done when detach
 *
 * 1.0.12
 * (1) Disable Auto AICR and WDT after register reset
 * (2) Fix PE20 chg_ops
 * (3) Do not HZ=1 and CHG_EN=0 for primary_chg when KPOC
 *
 * 1.0.11
 * (1) Add RT9471_REG_PUMPEXP to the reg lists
 * (2) Notify CHARGER_DEV_NOTIFY_EOC in rt9471_ieoc_irq_handler()
 *
 * 1.0.10
 * (1) Should not enter CV tracking in sys_min
 * (2) Rearrange the resources alloc and free in driver probing/removing
 * (3) Schedule psy_dwork with 1s delay time when getting chg psy fails
 *
 * 1.0.9
 * (1) Defer getting chg psy to rt9471_inform_psy_work_handler()
 * (2) Move all charger status checking during probing to rt9471_check_chg()
 * (3) Revise wakeup sources
 * (4) Add CONFIG_MTK_EXTERNAL_CHARGER_TYPE_DETECT
 * (5) Add support for AICC/PE10/PE20
 * (6) Add vac_ovp setting
 * (7) Rearrange the functions and remove #if 0 blocks
 * (8) Revise dual charging, including the usage of ceb_gpio
 * (9) Add chip_rev printing
 * (10) Add more charger_dev_notify() notifications
 *
 * 1.0.8
 * (1) Schedule a work to inform psy changed
 * (2) Revise the flow for shutdown and driver removing
 *
 * 1.0.7
 * (1) Revise the flow for entering shipping mode
 *
 * 1.0.6
 * (1) kthread_stop() at failure probing and driver removing
 * (2) disable_irq() at shutdown and driver removing
 * (3) Always inform psy changed if cable unattach
 * (4) Remove suspend_lock
 * (5) Stay awake during bc12_en
 * (6) Update irq_maskall from new datasheet
 * (7) Add the workaround for not leaving battery supply mode
 *
 * 1.0.5
 * (1) Add suspend_lock
 * (2) Add support for RT9470/RT9470D
 * (3) Sync with LK Driver
 * (4) Use IRQ to wait chg_rdy
 * (5) disable_irq()/enable_irq() in suspend()/resume()
 * (6) bc12_en in the kthread
 *
 * 1.0.4
 * (1) Use type u8 for regval in __rt9471_i2c_read_byte()
 *
 * 1.0.3
 * (1) Add shipping mode sys node
 * (2) Keep D+ at 0.6V after DCP got detected
 *
 * 1.0.2
 * (1) Kick WDT in __rt9471_dump_registers()
 *
 * 1.0.1
 * (1) Keep mivr via chg_ops
 *
 * 1.0.0
 * (1) Initial released
 */ 
