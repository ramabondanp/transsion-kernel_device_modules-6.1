/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 MediaTek Inc.
 */
#ifndef _HANG_UNWIND_H
#define _HANG_UNWIND_H
extern unsigned int hang_kernel_trace(struct task_struct *tsk,
					struct unwind_state *store, unsigned int size);
extern const char *hang_arch_vma_name(struct vm_area_struct *vma);
#endif
