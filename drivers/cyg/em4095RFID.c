#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/time.h>
#include <linux/timekeeping.h>
/***************************************************************
Copyright © Cyg Co., Ltd. 1998-2029. All rights reserved.
文件名		: em4095RFID.c
作者	  	: VicZhang
版本	   	: V1.0
描述	   	: EM4095采集RFID码值驱动程序。
其他	   	: 无
论坛 	   	: www.contron.com.cn
日志	   	: 初版V1.0 2019/8/20 VicZhang创建
***************************************************************/
#define MISC4095RFID_NAME "em4095RFID"
#define MISC4095RFID_MINOR MISC_DYNAMIC_MINOR /* 子设备号 */

/* misc4095RFID设备结构体 */
struct misc4095RFID_dev {
	struct device_node *nd; /* 设备节点 */
	struct gpio_desc *rfid_demod_out; /* 4095所使用的GPIO编号		*/
	struct gpio_desc *rfid_shd;
	struct mutex em4095_lock;
};

struct misc4095RFID_dev misc4095RFID; /* em4095设备 */

static int HOLD_TIME1 = (600000); //600 //600us
static int HOLD_TIME2 = (300000); //300 //300us

#define DATA (gpiod_get_value(misc4095RFID.rfid_demod_out) != 0)

static u8 RF_serial_55bits[55];

static s64 nsStart, nsNow;
static void start_timer(void)
{
	ktime_t ts;
	ts = ktime_get();
	nsStart = ktime_to_ns(ts);
}
static void clear_timer(void)
{
	ktime_t ts;
	ts = ktime_get();
	nsStart = ktime_to_ns(ts);
}
static int read_timer(void)
{
	ktime_t ts;
	ts = ktime_get();
	nsNow = ktime_to_ns(ts);
	return (int)((nsNow - nsStart));
}

static bool verify_parity(void)
{
	u8 i;

	for (i = 0; i < 10; i++) //行(偶)校验  ^异或运算
	{
		if (RF_serial_55bits[i * 5] ^ RF_serial_55bits[i * 5 + 1] ^
		    RF_serial_55bits[i * 5 + 2] ^ RF_serial_55bits[i * 5 + 3] ^
		    RF_serial_55bits[i * 5 + 4])
			return false;
	}
	for (i = 0; i < 4; i++) //列(偶)校验
	{
		if (RF_serial_55bits[i] ^ RF_serial_55bits[i + 5] ^
		    RF_serial_55bits[i + 10] ^ RF_serial_55bits[i + 15] ^
		    RF_serial_55bits[i + 20] ^ RF_serial_55bits[i + 25] ^
		    RF_serial_55bits[i + 30] ^ RF_serial_55bits[i + 35] ^
		    RF_serial_55bits[i + 40] ^ RF_serial_55bits[i + 45] ^
		    RF_serial_55bits[i + 50])
			return false;
	}
	if (RF_serial_55bits[54]) //检测最后一位是否为0，为0才是正确
		return false;
	for (i = 0; i < 5; i++) //集成5字节数据
	{
		RF_serial_55bits[i] = ((RF_serial_55bits[i * 10] << 7) |
				       (RF_serial_55bits[i * 10 + 1] << 6) |
				       (RF_serial_55bits[i * 10 + 2] << 5) |
				       (RF_serial_55bits[i * 10 + 3] << 4) |
				       (RF_serial_55bits[i * 10 + 5] << 3) |
				       (RF_serial_55bits[i * 10 + 6] << 2) |
				       (RF_serial_55bits[i * 10 + 7] << 1) |
				       RF_serial_55bits[i * 10 + 8]);
	}
	return true;
}

static bool read_rfid(u8 *rfid) //读码
{
	bool flag_rfid_syn = false; //syn标志，=1表示已同步
	bool flag_rfid_last = false;
	u8 i = 0, j;
	int value_timer = 0;

	start_timer(); //开启定时器

	while (i < 64) //找同步头
	{
		for (j = 0; j < 9; j++) {
			clear_timer();
			while (DATA == flag_rfid_last) {
				value_timer = read_timer();
				if (value_timer > HOLD_TIME1)
					return false;
			}

			if ((0 == flag_rfid_last && value_timer > HOLD_TIME2) ||
			    ( //0中间的上跳
				    1 == flag_rfid_last &&
				    value_timer <= HOLD_TIME2)) //0开始的下跳
			{
				if (1 == flag_rfid_last) {
					clear_timer();
					while (0 == DATA) {
						value_timer = read_timer();
						if (value_timer > HOLD_TIME2)
							return false;
					}
				}
				flag_rfid_last = true;
				i++;
				break;
			} else if ((0 == flag_rfid_last &&
				    value_timer <= HOLD_TIME2) ||
				   ( //1结束的上跳，表示将到来的码元可能为1
					   1 == flag_rfid_last &&
					   value_timer >
						   HOLD_TIME2)) //1中间的下跳
			{
				if (0 == flag_rfid_last) {
					clear_timer();
					while (DATA) //计算高电平维持时间
					{
						value_timer = read_timer();
						if (value_timer > HOLD_TIME2)
							return false;
					}
				}
				flag_rfid_last = false;
				i++;
			}
		}
		if (9 == j) //如果找到同步头，立刻跳出循环
		{
			flag_rfid_syn = true;
			break;
		}
	}

	if (!flag_rfid_syn) //如果没找到，跳出本函数，没有码片
		return false;

	for (i = 0; i < 55; i++) //接收55位数据
	{
		clear_timer();
		while (DATA == flag_rfid_last) {
			value_timer = read_timer();
			if (value_timer > HOLD_TIME1)
				return (false);
		}

		if ((0 == flag_rfid_last && value_timer > HOLD_TIME2) ||
		    ( //0中间的上跳
			    1 == flag_rfid_last &&
			    value_timer <= HOLD_TIME2)) //0开始的下跳
		{
			if (1 == flag_rfid_last) {
				clear_timer();
				while (0 == DATA) {
					value_timer = read_timer();
					if (value_timer > HOLD_TIME2)
						return false;
				}
			}
			flag_rfid_last = true;
			RF_serial_55bits[i] = 0; //接收数据存储BUF  存0
		} else if ((0 == flag_rfid_last &&
			    value_timer <=
				    HOLD_TIME2) //1结束的上跳，表示将到来的码元可能为1
			   || (1 == flag_rfid_last &&
			       value_timer > HOLD_TIME2)) //1中间的下跳
		{
			if (0 == flag_rfid_last) {
				clear_timer();
				while (DATA) //计算高电平维持时间
				{
					value_timer = read_timer();
					if (value_timer > HOLD_TIME2)
						return false;
				}
			}
			flag_rfid_last = false;
			RF_serial_55bits[i] = 1; //接收数据BUF，存1
		}
		//先对55字节中前面5位进行校验
		if (5 == i) {
			if (RF_serial_55bits[0] ^ RF_serial_55bits[1] ^
			    RF_serial_55bits[2] ^ RF_serial_55bits[3] ^
			    RF_serial_55bits[4])
				return false;
		}
	}

	//======55bit全部收完=======================================
	if (55 == i) {
		if (verify_parity()) //如果校验通过
		{
			if ((RF_serial_55bits[0] == 0) &&
			    (RF_serial_55bits[1] == 0) &&
			    (RF_serial_55bits[2] == 0) &&
			    (RF_serial_55bits[3] == 0) &&
			    (RF_serial_55bits[4] == 0))
				return false; //如果5字节都是0，说明没有码返回假
			else {
				rfid[4] = RF_serial_55bits[4];
				rfid[3] = RF_serial_55bits[3];
				rfid[2] = RF_serial_55bits[2];
				rfid[1] = RF_serial_55bits[1];
				rfid[0] = RF_serial_55bits[0];
				return true;
			}
		}
	}
	return false;
}

/*
 * @description		: 打开设备
 * @param - inode 	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 * 					  一般在open的时候将private_data指向设备结构体。
 * @return 			: 0 成功;其他 失败
 */
static int misc4095RFID_open(struct inode *inode, struct file *filp)
{
	if (mutex_lock_interruptible(&misc4095RFID.em4095_lock)) {
		return -ERESTARTSYS;
	}
	filp->private_data = &misc4095RFID; /* 设置私有数据 */
	gpiod_set_value(misc4095RFID.rfid_shd, 0);

	return 0;
}

/*
 * @description		: 从设备读取数据 
 * @param - filp 	: 要打开的设备文件(文件描述符)
 * @param - buf 	: 返回给用户空间的数据缓冲区
 * @param - cnt 	: 要读取的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 读取的字节数，如果为负值，表示读取失败
 */
static ssize_t misc4095RFID_read(struct file *filp, char __user *buf,
				 size_t cnt, loff_t *offt)
{
	uint8_t rfid_arr[5] = { 0 };
	int32_t i;
	bool res;
	unsigned long flags;

	for (i = 0; i != 50; ++i) {
		local_irq_save(flags);
		res = read_rfid(rfid_arr);
		local_irq_restore(flags);

		if (res)
			break;
	}

	if (copy_to_user((void *)buf, (const void *)rfid_arr,
			 sizeof(rfid_arr)) == 0) {
#if 0
		printk("RFID:");
		for(i = 0; i != 5; ++i)
		{
			printk("%02X", rfid_arr[i]);
		}
		printk("\r\n");
#endif
		return sizeof(rfid_arr);
	}
	return -1;
}

/*
 * @description		: 向设备写数据 
 * @param - filp 	: 设备文件，表示打开的文件描述符
 * @param - buf 	: 要写给设备写入的数据
 * @param - cnt 	: 要写入的数据长度
 * @param - offt 	: 相对于文件首地址的偏移
 * @return 			: 写入的字节数，如果为负值，表示写入失败
 */
static ssize_t misc4095RFID_write(struct file *filp, const char __user *buf,
				  size_t cnt, loff_t *offt)
{
	return 0;
}

/*
 * @description		: 关闭设备
 * @param - inode 	: 传递给驱动的inode
 * @param - filp 	: 设备文件，file结构体有个叫做private_data的成员变量
 * 					  一般在open的时候将private_data指向设备结构体。
 * @return 			: 0 成功;其他 失败
 */
static int misc4095RFID_release(struct inode *inode, struct file *filp)
{
	struct misc4095RFID_dev *devp = filp->private_data;

	gpiod_set_value(devp->rfid_shd, 1);
	/* 释放互斥锁 */
	mutex_unlock(&devp->em4095_lock);

	return 0;
}

/* 设备操作函数 */
static struct file_operations misc4095RFID_fops = {
	.owner = THIS_MODULE,
	.open = misc4095RFID_open,
	.read = misc4095RFID_read,
	.write = misc4095RFID_write,
	.release = misc4095RFID_release,
};

/* MISC设备结构体 */
static struct miscdevice em4095_miscdev = {
	.minor = MISC4095RFID_MINOR,
	.name = MISC4095RFID_NAME,
	.fops = &misc4095RFID_fops,
};

/*
  * @description     : flatform驱动的probe函数，当驱动与
  *                    设备匹配以后此函数就会执行
  * @param - dev     : platform设备
  * @return          : 0，成功;其他负值,失败
  */
static int misc4095RFID_probe(struct platform_device *dev)
{
	int ret = 0;

	dev_info(&dev->dev, "em4095 driver and device was matched!\r\n");

	mutex_init(&misc4095RFID.em4095_lock);
	misc4095RFID.nd = of_find_node_by_path("/em4095RFID");
	if (misc4095RFID.nd == NULL) {
		dev_err(&dev->dev, "em4095RFID node not find!\r\n");
		return -EINVAL;
	}

	misc4095RFID.rfid_shd =
		devm_gpiod_get(&dev->dev, "rfid_shd", GPIOD_OUT_HIGH);
	if (misc4095RFID.rfid_shd < 0) {
		dev_err(&dev->dev, "can't get rfid_shd");
		return -EINVAL;
	}
	misc4095RFID.rfid_demod_out =
		devm_gpiod_get(&dev->dev, "rfid_demod_out", 0);
	if (misc4095RFID.rfid_demod_out < 0) {
		dev_err(&dev->dev, "can't get rfid_demod_out");
		return -EINVAL;
	}

	/* 3、设置SHD为输出，并且输出高电平，默认关闭em4095 */
	ret = gpiod_direction_output(misc4095RFID.rfid_shd, 1);
	if (ret < 0) {
		dev_warn(&dev->dev, "can't set rfid_shd gpio!\r\n");
	}

	ret = gpiod_direction_input(misc4095RFID.rfid_demod_out);
	if (ret < 0) {
		dev_warn(&dev->dev, "can't set rfid_demod_out input gpio!\r\n");
	}

	ret = misc_register(&em4095_miscdev);
	if (ret < 0) {
		dev_warn(&dev->dev, "misc device register failed!\r\n");
		return -EFAULT;
	}

	return 0;
}

/*
 * @description     : platform驱动的remove函数，移除platform驱动的时候此函数会执行
 * @param - dev     : platform设备
 * @return          : 0，成功;其他负值,失败
 */
static int misc4095RFID_remove(struct platform_device *dev)
{
	/* 注销设备的时候关闭LED灯 */
	gpiod_set_value(misc4095RFID.rfid_shd, 1);

	/* 注销misc设备 */
	misc_deregister(&em4095_miscdev);
	return 0;
}

/* 匹配列表 */
static const struct of_device_id em4095RFID_of_match[] = {
	{ .compatible = "em4095RFID-V1" },
	{ /* Sentinel */ }
};

/* platform驱动结构体 */
static struct platform_driver em4095RFID_driver = {
     .driver     = {
         .name   = "em4095RFID-V1",         /* 驱动名字，用于和设备匹配 */
         .of_match_table = em4095RFID_of_match, /* 设备树匹配表          */
     },
     .probe      = misc4095RFID_probe,
     .remove     = misc4095RFID_remove,
};

/*
 * @description	: 驱动出口函数
 * @param 		: 无
 * @return 		: 无
 */
static int __init misc4095RFID_init(void)
{
	return platform_driver_register(&em4095RFID_driver);
}

/*
 * @description	: 驱动出口函数
 * @param 		: 无
 * @return 		: 无
 */
static void __exit misc4095RFID_exit(void)
{
	platform_driver_unregister(&em4095RFID_driver);
}

module_init(misc4095RFID_init);
module_exit(misc4095RFID_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("VicZhang");
