#include <linux/module.h>
#include <linux/netlink.h>
#include <net/genetlink.h>
#include <linux/workqueue.h>

#define RFID_NETLINK_VERSION 1
#define RFID_NETLINK_NAME "rfid_netlink"

enum rfid_multicast_cmd {
	RN_RFID,
};

enum rfid_netlink_commands {
	RN_CMD_UNSPEC,
	RN_CMD_GET_VERSION,
	RN_CMD_DEV_UP,
	RN_CMD_DEV_DOWN,
	RN_CMD_DEV_SET,
	RN_CMD_DEV_GET,
};

enum rfid_netlink_attrs {
	RFID_NETLINK_ATTR_UNSPEC,
	RFID_NETLINK_ATTR_MSG,
	RFID_NETLINK_ATTR_RFID,
	__RFID_NETLINK_ATTR_MAX,
};

#define RFID_NETLINK_ATTR_MAX (__RFID_NETLINK_ATTR_MAX - 1)
static struct genl_family rfid_gnl_family;
static struct delayed_work dw;

static const struct nla_policy rn_policy[RFID_NETLINK_ATTR_MAX + 1] = {
	[RFID_NETLINK_ATTR_MSG] = { .type = NLA_NUL_STRING },
	[RFID_NETLINK_ATTR_RFID] = { .type = NLA_U64 },
};

int rn_get_version(struct sk_buff *skb, struct genl_info *info)
{
	nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);

	genlmsg_put(skb, info->snd_portid, info->snd_seq, &rfid_gnl_family, 0,
		    RN_CMD_GET_VERSION);

	nla_put_string(skb, RFID_NETLINK_ATTR_MSG, "rfid_netlink version 1.0");

	genlmsg_end(skb, info);

	return genlmsg_reply(skb, info);
}

int rn_dev_up(struct sk_buff *skb, struct genl_info *info)
{
	printk("rn_dev_up\n");

	return 0;
}

int rn_dev_down(struct sk_buff *skb, struct genl_info *info)
{
	printk("rn_dev_down\n");

	return 0;
}

static const struct genl_small_ops rn_ops[] = {
	{
		.cmd = RN_CMD_GET_VERSION,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = rn_get_version,
	},

	{
		.cmd = RN_CMD_DEV_UP,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = rn_dev_up,
	},
	{
		.cmd = RN_CMD_DEV_DOWN,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit = rn_dev_down,
	},
};

static const struct genl_multicast_group mcgrps[] = {
	{ .name = "rfid" },
};

static struct genl_family rfid_gnl_family = {
	.hdrsize = 0,
	.name = RFID_NETLINK_NAME,
	.version = RFID_NETLINK_VERSION,
	.maxattr = RFID_NETLINK_ATTR_MAX,
	.small_ops = rn_ops,
	.n_small_ops = ARRAY_SIZE(rn_ops),
	.module = THIS_MODULE,
	.policy = rn_policy,
	.mcgrps = mcgrps,
	.n_mcgrps = ARRAY_SIZE(mcgrps),
};

void worker_func(struct work_struct *work)
{
	u64 rfid = 0x1234567890abcdef;
	struct sk_buff *skb;
	void *hdr;

	pr_err("worker_func\n");
	skb = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!skb) {
		pr_err("genlmsg_new failed\n");
		return;
	}

	hdr = genlmsg_put(skb, 0, 0, &rfid_gnl_family, 0, RN_RFID);
	if (!hdr) {
		pr_err("genlmsg_put failed\n");
		goto out;
	}

	nla_put_64bit(skb, RFID_NETLINK_ATTR_MSG, 8, &rfid, 0);

	genlmsg_end(skb, hdr);
	genlmsg_multicast(&rfid_gnl_family, skb, 0, 0, GFP_KERNEL);


	schedule_delayed_work(&dw, msecs_to_jiffies(5000));

	return;
out:
	nlmsg_free(skb);
}

static int __init rfid_netlink_init(void)
{

    printk(KERN_INFO "rfid_netlink: init\n");

    genl_register_family(&rfid_gnl_family);

    INIT_DELAYED_WORK(&dw, worker_func);
    /* schedule_delayed_work(&dw, msecs_to_jiffies(5000)); */

    return 0;
}

static void __exit rfid_netlink_exit(void)
{
    printk(KERN_INFO "rfid_netlink: exit");
}

module_init(rfid_netlink_init);
module_exit(rfid_netlink_exit);
