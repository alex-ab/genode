/*
 * \brief  Shared-memory file utility
 * \author Christian Helmuth
 * \author Josef Söntgen
 * \date   2023-12-04
 *
 * This utility implements limited shared-memory file semantics as required by
 * Linux graphics drivers (e.g., intel_fb and lima_gpu_drv)
 */

/*
 * Copyright (C) 2023 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <lx_emul/shared_dma_buffer.h>
#include <linux/shmem_fs.h>

struct shmem_file_buffer
{
	struct genode_shared_dataspace *dataspace;
	void *addr;
	struct page *pages;
};


struct file *shmem_file_setup(char const *name, loff_t size,
                              unsigned long flags)
{
	struct file *f;
	struct inode *inode;
	struct address_space *mapping;
	struct shmem_file_buffer *i_private_data;
	loff_t const nrpages = DIV_ROUND_UP(size, PAGE_SIZE);

	if (!size)
		return (struct file*)ERR_PTR(-EINVAL);

	f = kzalloc(sizeof (struct file), GFP_KERNEL);
	if (!f) {
		return (struct file*)ERR_PTR(-ENOMEM);
	}

	inode = kzalloc(sizeof (struct inode), GFP_KERNEL);
	if (!inode) {
		goto err_inode;
	}

	mapping = kzalloc(sizeof (struct address_space), GFP_KERNEL);
	if (!mapping) {
		goto err_mapping;
	}

	i_private_data = kzalloc(sizeof (struct shmem_file_buffer), GFP_KERNEL);
	if (!i_private_data) {
		goto err_private_data;
	}

	i_private_data->dataspace = lx_emul_shared_dma_buffer_allocate(nrpages * PAGE_SIZE);
	if (!i_private_data->dataspace)
		goto err_private_data_addr;

	i_private_data->addr = lx_emul_shared_dma_buffer_virt_addr(i_private_data->dataspace);
	i_private_data->pages = lx_emul_virt_to_page(i_private_data->addr);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,18,0)
	mapping->i_private_data = i_private_data;
#else
	mapping->private_data = i_private_data;
#endif
	mapping->nrpages = nrpages;

	inode->i_mapping = mapping;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,18,0)
	file_ref_init(&f->f_ref, 1);
#else
	atomic_long_set(&f->f_count, 1);
#endif
	f->f_inode    = inode;
	f->f_mapping  = mapping;
	f->f_flags    = flags;
	f->f_mode     = OPEN_FMODE(flags);
	f->f_mode    |= FMODE_OPENED;

	return f;

err_private_data_addr:
	kfree(i_private_data);
err_private_data:
	kfree(mapping);
err_mapping:
	kfree(inode);
err_inode:
	kfree(f);
	return (struct file*)ERR_PTR(-ENOMEM);
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
#define folio_cast
struct page *shmem_read_mapping_page_gfp(struct address_space *mapping,
                                         pgoff_t index, gfp_t gfp)
#else
#define folio_cast (struct folio *)
struct folio *shmem_read_folio_gfp(struct address_space *mapping,
                                   pgoff_t index, gfp_t gfp)
#endif
{
	struct page *p;
	struct shmem_file_buffer *i_private_data;

	if (index > mapping->nrpages)
		return NULL;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,18,0)
	i_private_data = mapping->i_private_data;
#else
	i_private_data = mapping->private_data;
#endif

	p = i_private_data->pages;
	return folio_cast(p + index);
}


#include <linux/pagevec.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
void __pagevec_release(struct pagevec * pvec)
{
	/* XXX check if we have to call release_pages */
	pagevec_reinit(pvec);
}
#else
void __folio_batch_release(struct folio_batch *fbatch)
{
	lx_emul_trace(__func__);

	/* XXX check if we have to call release_pages */
	folio_batch_reinit(fbatch);
}
#endif


#include <linux/file.h>

static void _free_file(struct file *file)
{
	struct inode *inode;
	struct address_space *mapping;
	struct shmem_file_buffer *i_private_data;

	mapping      = file->f_mapping;
	inode        = file->f_inode;

	if (mapping) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,18,0)
		i_private_data = mapping->i_private_data;
#else
		i_private_data = mapping->private_data;
#endif

		lx_emul_shared_dma_buffer_free(i_private_data->dataspace);

		kfree(i_private_data);
		kfree(mapping);
	}

	kfree(inode);
	kfree(file->f_path.dentry);
	kfree(file);
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,18,0)
/*
 * Used below from within 'file_ref_put()' and put here
 * to prevent adding code to the handful of drivers
 * making use of this header file.
 */
bool __file_ref_put(file_ref_t * ref,unsigned long cnt)
{
	return cnt == FILE_REF_NOREF;
}
#endif


void fput(struct file *file)
{
	if (!file)
		return;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,18,0)
	if (file_ref_put(&file->f_ref))
#else
	if (atomic_long_sub_and_test(1, &file->f_count))
#endif
		_free_file(file);
}
