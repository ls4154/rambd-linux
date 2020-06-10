#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/types.h>

static const unsigned long rambd_sector_size = 512;
static unsigned long rambd_sectors = 1024 * 1024; /* default device size 512 MiB */
module_param(rambd_sectors, ulong, 0);

static int major;
static const char *rambd_name = "rambd";

struct rambd_device {
	u8 *data;
	struct request_queue *queue;
	struct gendisk *disk;
};

static struct rambd_device device;

static int rambd_ioctl(struct block_device *bdev, fmode_t mode,
			unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}


static const struct block_device_operations rambd_fops = {
	.owner = THIS_MODULE,
	.ioctl = rambd_ioctl,
};

static blk_qc_t rambd_make_request(struct request_queue *q, struct bio *bio)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 14, 0)
	struct gendisk *disk = bio->bi_bdev->bd_disk;
#else
	struct gendisk *disk = bio->bi_disk;
#endif
	struct rambd_device *rambd = disk->private_data;
	struct bio_vec bvec;
	struct bvec_iter iter;
	void *mem;

	if (bio_end_sector(bio) > get_capacity(disk)) {
		bio_io_error(bio);
		return BLK_QC_T_NONE;
	}

	bio_for_each_segment(bvec, bio, iter) {
		mem = kmap_atomic(bvec.bv_page);
		if (mem == NULL) {
			printk("rambd: kmap failed\n");
		}
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 10, 0)
		if (bio_data_dir(bio) == WRITE) {
#else
		if (op_is_write(bio_op(bio))) {
#endif
			memcpy(rambd->data + bio->bi_iter.bi_sector * rambd_sector_size, mem + bvec.bv_offset, bvec.bv_len);
		} else {
			memcpy(mem + bvec.bv_offset, rambd->data + bio->bi_iter.bi_sector * rambd_sector_size, bvec.bv_len);
		}
		kunmap_atomic(mem);
	}

	bio_endio(bio);
	return BLK_QC_T_NONE;
}

static int __init rambd_init(void)
{
	device.data = vmalloc(rambd_sectors * rambd_sector_size);
	if (device.data == NULL) {
		printk("rambd: failed to allocate memory.\n");
		return -ENOMEM;
	}

	major = register_blkdev(0, rambd_name);
	if (major < 0) {
		printk("rambd: failed to register device.\n");
		vfree(device.data);
		return -1;
	}

	device.queue = blk_alloc_queue(GFP_KERNEL);
	if (device.queue == NULL) {
		printk("rambd: failed to allocate queue.\n");
		vfree(device.data);
		return -1;
	}
	blk_queue_make_request(device.queue, rambd_make_request);
	blk_queue_max_hw_sectors(device.queue, 512);
	//blk_queue_physical_block_size(device.queue, PAGE_SIZE);

	device.disk = alloc_disk(1);
	if (device.disk == NULL) {
		printk("rambd: failed to allocate disk.\n");
		vfree(device.data);
		return -1;
	}

	device.disk->major = major;
	device.disk->first_minor = 0;
	device.disk->fops = &rambd_fops;
	device.disk->private_data = &device;
	device.disk->queue = device.queue;
	set_capacity(device.disk, rambd_sectors);
	strcpy(device.disk->disk_name, "rambd0");
	add_disk(device.disk);

	printk("rambd: init success\n");

	return 0;
}

static void __exit rambd_exit(void)
{
	del_gendisk(device.disk);
	put_disk(device.disk);
	unregister_blkdev(major, rambd_name);
	blk_cleanup_queue(device.queue);
	vfree(device.data);

	printk("rambd: exit success\n");
}

module_init(rambd_init);
module_exit(rambd_exit);

MODULE_AUTHOR("Youngjae Lee <ls4154.lee@gmail.com>");
MODULE_DESCRIPTION("Simple RAM block device");
MODULE_LICENSE("GPL");
