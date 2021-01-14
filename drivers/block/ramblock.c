#include <linux/major.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/blk-mq.h>
#include <linux/hdreg.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("DRAAPHO");

#define DEVICE_NAME "RAMDISK"
#define RAMBLOCK_SIZE (1024*1024)

static int major;
static struct gendisk *ramblock_disk;
static struct request_queue *ramblock_queue;
static DEFINE_SPINLOCK(ramblock_lock);
static unsigned char *ramblock_buf;

// 分区需要知道"硬盘"的几何结构(geometry), 这里虚拟一下即可.
static int ramblock_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	// 磁头数=盘面数*2
	geo->heads = 2;
	// 柱面数
	geo->cylinders = 32;
	// 扇区数. 利用公式: 存储容量=磁头数x柱面数x扇区数x512
	geo->sectors = RAMBLOCK_SIZE/2/32/512;

	return 0;
}

static struct block_device_operations ramblock_fops = {
	.owner  = THIS_MODULE,
	.getgeo = ramblock_getgeo,
};

static blk_status_t do_ramblock_request(struct blk_mq_hw_ctx *hctx,
				const struct blk_mq_queue_data *bd)
{
	struct request *req = bd->rq;
	void *buffer = bio_data(req->bio);
	unsigned long offset = blk_rq_pos(req) << 9;
	unsigned long len  = blk_rq_cur_bytes(req);

	blk_mq_start_request(req);
	if (rq_data_dir(req) == READ) {
		// 读操作
		printk("do_ramblock_request read %ld\n", len);
		// 直接读 ramblock_buf
		memcpy(buffer, ramblock_buf+offset, len);
	} else {
		// 写操作
		printk("do_ramblock_request write %ld\n", len);
		// 直接写 ramblock_buf
		memcpy(ramblock_buf+offset, buffer, len);
	}
	blk_mq_end_request(req, BLK_STS_OK);
	return BLK_STS_OK;
}

static const struct blk_mq_ops ramblock_mq_ops = {
	.queue_rq	= do_ramblock_request,
};
static struct blk_mq_tag_set tag_set;
static struct kobject *ramblock_find(dev_t dev, int *part, void *data)
{
	*part = 0;
	return get_disk_and_module(ramblock_disk);
}

static int ramblock_init(void)
{
	/* 1. 分配一个gendisk结构体 */
	// 次设备号个数, 也是允许的最大分区个数
	ramblock_disk = alloc_disk(16);

	/* 2. 设置 */
	/* 2.1 分配/设置缓冲队列 */
	ramblock_queue = blk_mq_init_sq_queue(&tag_set, &ramblock_mq_ops,
			16, BLK_MQ_F_SHOULD_MERGE);
	ramblock_disk->queue = ramblock_queue;

	/* 2.2 设置其他属性: 比如容量 */
	// cat /proc/devices 查看块设备
	major = register_blkdev(0, DEVICE_NAME);
	printk("major = %d\n", major);
	// 主设备号
	ramblock_disk->major       = major;
	// 次设备号起始值
	ramblock_disk->first_minor = 0;
	sprintf(ramblock_disk->disk_name, "ramblock");
	ramblock_disk->fops        = &ramblock_fops;
	// 设置扇区的数量, 不是字节数
	set_capacity(ramblock_disk, RAMBLOCK_SIZE / 512);

	/* 3. 硬件初始化操作 */
	ramblock_buf = kzalloc(RAMBLOCK_SIZE, GFP_KERNEL);

	/* 4. 注册 */
	add_disk(ramblock_disk);
    blk_register_region(MKDEV(major, 0), 8, THIS_MODULE,
				ramblock_find, NULL, NULL);
	return 0;
}

static void ramblock_exit(void)
{
	// 对应 add_disk
	del_gendisk(ramblock_disk);
	// 对应 blk_init_queue
	put_disk(ramblock_disk);
	// 对应 blk_init_queue
	blk_cleanup_queue(ramblock_queue);
	// 对应 register_blkdev
	unregister_blkdev(major, DEVICE_NAME);
	// 安全起见, 最后释放buf
	kfree(ramblock_buf);
}

module_init(ramblock_init);
module_exit(ramblock_exit);
