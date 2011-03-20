/*
	Copyright (C) 2011 Greg Bowyer <gbowyer@fastmail.co.uk>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "az_mm_debug.h"
#include <linux/debugfs.h>

static const int LINE_SIZE = 40;
	/*
	(sizeof(char) * (ceil((sizeof(long) - 1) * log10(2)) * 3)) + 
	(sizeof(char) * (ceil((sizeof(int) - 1) * log10(2)) * 2)) +
	(sizeof(char) * (ceil(sizeof(unsigned long) * log10(2)))) +
	(sizeof(char) * 25);
	*/

/* Shamelessly ripped off from proc */
extern int az_mm_create_vmem_map_dump(struct vm_area_struct *vma)
{
	if (!az_mm_debug.debug_dir) {
		return 0;
	}
		
	char *buffer = kcalloc(LINE_SIZE, sizeof(char), GFP_KERNEL);
	if (buffer == NULL) {
		return -ENOMEM;
	}

	struct debugfs_blob_wrapper *map_blob;
	map_blob = (struct debugfs_blob_wrapper *)
		kmalloc(sizeof(struct debugfs_blob_wrapper), GFP_KERNEL);
	if (map_blob == NULL) {
		kfree(buffer);
		return -ENOMEM;
	}

	struct mm_struct *mm = vma->vm_mm;
	struct file *file = vma->vm_file;
	int flags = vma->vm_flags;
	unsigned long ino = 0;
	unsigned long long pgoff = 0;
	unsigned long start;
	dev_t dev = 0;
	int len;

	if (file) {
		struct inode *inode = vma->vm_file->f_path.dentry->d_inode;
		dev = inode->i_sb->s_dev;
		ino = inode->i_ino;
		pgoff = ((loff_t)vma->vm_pgoff) << PAGE_SHIFT;
	}

	/* don't show the stack guard page */
	start = vma->vm_start;
	if (vma->vm_flags & VM_GROWSDOWN)
		if (!vma_stack_continue(vma->vm_prev, vma->vm_start))
			start += PAGE_SIZE;

	snprintf(buffer, LINE_SIZE, 
			"%08lx-%08lx %c%c%c%c%c %08llx %02x:%02x %lu %n\n",
			vma->vm_start,
			vma->vm_end,
			flags & VM_READ ? 'r' : '-',
			flags & VM_WRITE ? 'w' : '-',
			flags & VM_EXEC ? 'x' : '-',
			flags & VM_MAYSHARE ? 's' : 'p',
			vma->mm_module_ops ? 'm' : '\0',
			((loff_t)vma->vm_pgoff) << PAGE_SHIFT,
			MAJOR(dev), MINOR(dev), ino, &len);

	map_blob->data = buffer;
	return debugfs_create_blob("failed_map", S_IRUSR, az_mm_debug.debug_dir, map_blob);
}

