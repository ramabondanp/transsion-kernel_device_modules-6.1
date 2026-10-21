#ifndef __TRAN_FP_EXT_H__
#define __TRAN_FP_EXT_H__

#include "tran_fp_main.h"

int tran_proc_init(struct tran_fp_data *spidev);

int gf_proc_init(struct tran_fp_data *tran_fp_dev);

int gf_proc_deinit(struct tran_fp_data *tran_fp_dev);
int gf_fp_irq_init(void);

#ifdef CONFIG_HAS_EARLYSUSPEND
void tran_early_suspend(struct early_suspend *handler);
void tran_late_resume(struct early_suspend *handler);

#else
int tran_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data);

#endif
#endif

