/*-
 * GPL LICENSE SUMMARY
 *
 *   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *   The full GNU General Public License is included in this distribution
 *   in the file called LICENSE.GPL.
 *
 *   Contact Information:
 *   Intel Corporation
 */


#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/rcupdate.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/mmu_context.h>
#include <linux/sched.h>
#include <asm/mmu_context.h>
#include <linux/fdtable.h>

#include "fd_link.h"


/*
 * get_files_struct is copied from fs/file.c
 */
struct files_struct *
get_files_struct (struct task_struct *task)
{
	struct files_struct *files;

	task_lock (task);
	files = task->files;
	if (files)
		atomic_inc (&files->count);
	task_unlock (task);

	return files;
}

/*
 * put_files_struct is extracted from fs/file.c
 */
void
put_files_struct (struct files_struct *files)
{
	if (atomic_dec_and_test (&files->count))
	{
		BUG ();
	}
}


static long
fd_link_ioctl (struct file *f, unsigned int ioctl, unsigned long arg)
{
	void __user *argp = (void __user *) arg;
	struct task_struct *task_target = NULL;
	struct file *file;
	struct files_struct *files;
	struct fdtable *fdt;
	struct fd_copy fd_copy;

	switch (ioctl)
	{
		case FD_COPY:
			if (copy_from_user (&fd_copy, argp, sizeof (struct fd_copy)))
				return -EFAULT;

			/*
			 * Find the task struct for the target pid
			 */
			task_target =
				pid_task (find_vpid (fd_copy.target_pid), PIDTYPE_PID);
			if (task_target == NULL)
			{
				printk (KERN_DEBUG "Failed to get mem ctx for target pid\n");
				return -EFAULT;
			}

			files = get_files_struct (current);
			if (files == NULL)
			{
				printk (KERN_DEBUG "Failed to get files struct\n");
				return -EFAULT;
			}

			rcu_read_lock ();
			file = fcheck_files (files, fd_copy.source_fd);
			if (file)
			{
				if (file->f_mode & FMODE_PATH
						|| !atomic_long_inc_not_zero (&file->f_count))
					file = NULL;
			}
			rcu_read_unlock ();
			put_files_struct (files);

			if (file == NULL)
			{
				printk (KERN_DEBUG "Failed to get file from source pid\n");
				return 0;
			}

			/*
			 * Release the existing fd in the source process
			 */
			spin_lock (&files->file_lock);
			filp_close (file, files);
			fdt = files_fdtable (files);
			fdt->fd[fd_copy.source_fd] = NULL;
			spin_unlock (&files->file_lock);

			/*
			 * Find the file struct associated with the target fd.
			 */

			files = get_files_struct (task_target);
			if (files == NULL)
			{
				printk (KERN_DEBUG "Failed to get files struct\n");
				return -EFAULT;
			}

			rcu_read_lock ();
			file = fcheck_files (files, fd_copy.target_fd);
			if (file)
			{
				if (file->f_mode & FMODE_PATH
						|| !atomic_long_inc_not_zero (&file->f_count))
					file = NULL;
			}
			rcu_read_unlock ();
			put_files_struct (files);

			if (file == NULL)
			{
				printk (KERN_DEBUG "Failed to get file from target pid\n");
				return 0;
			}


			/*
			 * Install the file struct from the target process into the
			 * file desciptor of the source process,
			 */

			fd_install (fd_copy.source_fd, file);

			return 0;

		default:
			return -ENOIOCTLCMD;
	}
}

static const struct file_operations fd_link_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fd_link_ioctl,
};


static struct miscdevice fd_link_misc = {
	.name = "fd-link",
	.fops = &fd_link_fops,
};

static int __init
fd_link_init (void)
{
	return misc_register (&fd_link_misc);
}

module_init (fd_link_init);

static void __exit
fd_link_exit (void)
{
	misc_deregister (&fd_link_misc);
}

module_exit (fd_link_exit);

MODULE_VERSION ("0.0.1");
MODULE_LICENSE ("GPL v2");
MODULE_AUTHOR ("Anthony Fee");
MODULE_DESCRIPTION ("Link fd");
MODULE_ALIAS ("devname:fd-link");
