/* az_mm_debug.h - Azul Virtual Memory Management
 *
 * Author Greg Bowyer <gbowyer@fastmail.co.uk>
 *
 * Copyright 2010 Greg Bowyer.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 * 
 * This code is free software; you can redistribute it and/or modify it under 
 * the terms of the GNU General Public License version 2 only, as published by 
 * the Free Software Foundation. 
 * 
 * This code is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License version 2 for  more
 * details (a copy is included in the LICENSE file that accompanied this code).
 * 
 * You should have received a copy of the GNU General Public License version 2 
 * along with this work; if not, write to the Free Software Foundation,Inc., 
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 */

#ifndef _LINUX_AZ_MM_DEBUG_H
#define _LINUX_AZ_MM_DEBUG_H

#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/mm.h>

#ifdef __KERNEL__

#define CONFIG_AZMM_DEBUG 1

struct az_mm_debug_details {
	struct dentry *debug_dir;
};

static struct az_mm_debug_details az_mm_debug = {
	.debug_dir = NULL
};

static inline void az_mm_debug_init() {
#ifdef CONFIG_AZMM_DEBUG
    az_mm_debug.debug_dir = debugfs_create_dir("azul_mm", NULL);
    if (! az_mm_debug.debug_dir) {
        printk(KERN_WARNING "unable to create 'azul_mm' debugfs directory\n");
    } else {
		printk(KERN_INFO "created AZUL debug directory\n");
	}
#endif
}

static inline void az_mm_debug_destroy() {
    if (az_mm_debug.debug_dir) {
        debugfs_remove_recursive(az_mm_debug.debug_dir);
    }
}

extern int az_mm_create_vmem_map_dump(struct vm_area_struct *vma);

#endif
#endif
