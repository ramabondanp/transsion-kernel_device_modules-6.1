// SPDX-FileCopyrightText: 2024 Infineon Technologies AG
//
// SPDX-License-Identifier: MIT

/**
 * @file   authon_platform.h
 * @date   Dec, 2023
 * @brief  configure GPIO of the platform 
 */
#ifndef __AUTHON_PLATFORM_H__
#define __AUTHON_PLATFORM_H__

#include <linux/types.h>
#include <linux/string.h>

typedef struct platform_opaque_context authon_platform_context;

typedef int (*GPIO_WRITE_FUNC)(authon_platform_context *ctx, int value);
typedef int (*GPIO_READ_FUNC)(authon_platform_context *ctx);

typedef int (*GPIO_CONF_FUNC)(authon_platform_context *ctx, int direction,
			      int value);
typedef int (*IRQ_DISABLE_FUNC)(authon_platform_context *ctx);

typedef int (*IRQ_RESTORE_FUNC)(authon_platform_context *ctx);

typedef int (*DELAY_US_FUNC)(authon_platform_context *ctx, uint32_t us);

typedef int (*DELAY_MS_FUNC)(authon_platform_context *ctx, uint32_t ms);

typedef uint64_t (*TICK_NS_FUNC)(authon_platform_context *ctx);

typedef int (*ALT_RNG_FUNC)(authon_platform_context *ctx, uint8_t random_byte,
			    size_t len);

typedef void (*PRINT_FUNC)(authon_platform_context *ctx, const char *fmt, ...);

typedef void (*PRINT_CONT_FUNC)(authon_platform_context *ctx, const char *fmt,
				...);

struct authon_platform_context_common {
	uint32_t magic;

#define SWI_GPIO_LEVEL_HIGH (1u)
#define SWI_GPIO_LEVEL_LOW (0u)
#define SWI_GPIO_DIRECTION_IN (0u)
#define SWI_GPIO_DIRECTION_OUT (1u)
	GPIO_CONF_FUNC gpio_conf;
	GPIO_WRITE_FUNC gpio_write;
	GPIO_READ_FUNC gpio_read;

	IRQ_DISABLE_FUNC irq_disable;
	IRQ_RESTORE_FUNC irq_restore;

	DELAY_US_FUNC delay_us;
	DELAY_MS_FUNC delay_ms;

	TICK_NS_FUNC tick_ns;

	ALT_RNG_FUNC alt_rng;

	PRINT_FUNC print;
	PRINT_CONT_FUNC print_cont;
};

#define authon_pf_gpio_conf(ctx, a, b) \
	((struct authon_platform_context_common *)(ctx))->gpio_conf(ctx, a, b)
#define authon_pf_gpio_write(ctx, a) \
	((struct authon_platform_context_common *)(ctx))->gpio_write(ctx, a)
#define authon_pf_gpio_read(ctx) \
	((struct authon_platform_context_common *)(ctx))->gpio_read(ctx)
#define authon_pf_irq_disable(ctx) \
	((struct authon_platform_context_common *)(ctx))->irq_disable(ctx)
#define authon_pf_irq_restore(ctx) \
	((struct authon_platform_context_common *)(ctx))->irq_restore(ctx)
#define authon_pf_delay_us(ctx, a) \
	((struct authon_platform_context_common *)(ctx))->delay_us(ctx, a)
#define authon_pf_delay_ms(ctx, a) \
	((struct authon_platform_context_common *)(ctx))->delay_ms(ctx, a)
#define authon_pf_tick_ns(ctx) \
	((struct authon_platform_context_common *)(ctx))->tick_ns(ctx)
#define authon_pf_alt_rng(ctx, a, b) \
	((struct authon_platform_context_common *)(ctx))->alt_rng(ctx, a, b)
#define authon_pf_print(ctx, a, ...)                     \
	((struct authon_platform_context_common *)(ctx)) \
		->print(ctx, a, ##__VA_ARGS__)
#define authon_pf_print_cont(ctx, a, ...)                \
	((struct authon_platform_context_common *)(ctx)) \
		->print_cont(ctx, a, ##__VA_ARGS__)

#endif /* __AUTHON_PLATFORM_H__ */
