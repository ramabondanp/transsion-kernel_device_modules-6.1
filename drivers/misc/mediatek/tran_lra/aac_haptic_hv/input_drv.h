#ifndef __ICS_INPUT_DRV_H__
#define __ICS_INPUT_DRV_H__

#include <linux/types.h>
#include "haptic_drv.h"

#define MAX_EFFECT_COUNT         10

int32_t ics_input_dev_register(struct ics_haptic_data *haptic_data);
int32_t ics_input_dev_remove(struct ics_haptic_data *haptic_data);
int32_t ics_input_irq_handler(void *data);

#endif // __ICS_INPUT_DRV_H__
