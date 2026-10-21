/*
 * (C) Copyright 2021-2022, Shenzhen Tetras.AI Technology Co., Ltd
 * This file is classified as confidential level C4 within Tetras.AI
 *
 * Change Logs:
 * Date         Author          Notes
 * 2022-03-03   Yao Kun         Initialize.
 */

/**
 * @brief AI ISP Driver Debug Header
 * @date  2022-03-03
 */

#ifndef _CAM_AI_ISP_DBG_H_
#define _CAM_AI_ISP_DBG_H_

#include <linux/platform_device.h>

/*
 * AIISP_ERR
 * @brief    :  This Macro will print error logs
 *
 * @__module :  Respective module id which is been calling this Macro
 * @fmt      :  Formatted string which needs to be print in log
 * @args     :  Arguments which needs to be print in log
 */
#define AIISP_ERR(__module, fmt, args...)                                      \
	({                                                                     \
		pr_err("AIISP_ERR: %s: %d " fmt "\n",                          \
			__func__, __LINE__, ##args);                           \
	})

/*
 * AIISP_WARN
 * @brief    :  This Macro will print warning logs
 *
 * @__module :  Respective module id which is been calling this Macro
 * @fmt      :  Formatted string which needs to be print in log
 * @args     :  Arguments which needs to be print in log
 */
#define AIISP_WARN(__module, fmt, args...)                                     \
	({                                                                     \
		pr_warn("AIISP_WARN: %s: %d " fmt "\n",                        \
			__func__, __LINE__, ##args);                           \
	})

/*
 * AIISP_DBG
 * @brief    :  This Macro will print debug logs
 *
 * @__module :  Respective module id which is been calling this Macro
 * @fmt      :  Formatted string which needs to be print in log
 * @args     :  Arguments which needs to be print in log
 */
#define AIISP_DBG(__module, fmt, args...)                                      \
	({                                                                     \
		pr_debug("AIISP_DEBUG: %s: %d " fmt "\n",                      \
			__func__, __LINE__, ##args);                           \
	})

/*
 * AIISP_INFO
 * @brief    :  This Macro will print Information logs
 *
 * @__module :  Respective module id which is been calling this Macro
 * @fmt      :  Formatted string which needs to be print in log
 * @args     :  Arguments which needs to be print in log
 */
#define AIISP_INFO(__module, fmt, args...)                                     \
	({                                                                     \
		pr_info("AIISP_INFO: %s: %d " fmt "\n",                        \
			__func__, __LINE__, ##args);                           \
	})

#endif /* _CAM_AI_ISP_DBG_H_ */
