// SPDX-License-Identifier: GPL-2.0-only
/*
 * gts3-blsp-owner - Galaxy Tab S3 BLSP QUP ownership test aid
 *
 * On this tablet Samsung's TrustZone owns the fingerprint SPI bus (BLSP1
 * QUP6, TLMM 25-28). Qualcomm's TrustZone of this generation can hand a
 * BLSP QUP between the secure world and the application processor at run
 * time, through a SIP SCM call (service 4, function 3,
 * "TZ_BLSP_MODIFY_OWNERSHIP"). Samsung's own downstream drivers use it to
 * share a bus with a secure-touch or secure-fingerprint trustlet:
 *
 *   drivers/input/touchscreen/stm/fts_ts.c:72,154   (Galaxy S7 hero2qlte)
 *   drivers/input/touchscreen/sec_ts/sec_ts.c:37-42,129-150
 *   drivers/soc/qcom/qbt1000.c:48-51,636-655
 *   (all in Samsung's GPL msm-3.18 kernel for msm8996)
 *
 * This module only asks TrustZone whether it will hand BLSP1 QUP6 to the
 * application processor. It does NOT touch the TLMM pins or the QUP
 * registers itself, and it does nothing at load time. It acts only when a
 * value is written to its debugfs trigger, and it logs the raw TrustZone
 * return code. It is a bring-up test aid, not upstream material.
 *
 * WHY A RAW SMC AND NOT qcom_scm: mainline qcom_scm exports no interface
 * for this SIP call, its QSEECOM path is gated to an allowlist of laptops,
 * and it offers no generic "raw SCM" entry point to out-of-tree code. So
 * this module issues the SMC directly with arm_smccc_smc(), reproducing
 * exactly the register layout of Samsung's scm_call2() for this call:
 *
 *   downstream scm_call2(fn_id, desc), arglen <= N_REGISTER_ARGS, armv8-64
 *   (drivers/soc/qcom/scm.c:701, __scm_call_armv8_64 at :570/:415):
 *     x0 = fn_id | SMC64_MASK
 *     x1 = desc->arginfo          (SCM_ARGS(2) = 2)
 *     x2 = desc->args[0]          (BLSP/QUP index)
 *     x3 = desc->args[1]          (new owner: TZ=1, APSS=3)
 *     x4 = desc->args[2]          (0, unused; arglen is 2)
 *     x5 = 0
 *   fn_id = SCM_SIP_FNID(4, 3) = 0x02000403 (include/soc/qcom/scm.h:48)
 *   returns x0 = status, retried while x0 == SCM_INTERRUPTED (1).
 *
 * arm_smccc_smc() maps a0..a7 -> x0..x7 and res->a0..a3 <- x0..x3, so the
 * SMC issued here is byte-for-byte the one Samsung's kernel issues.
 */

#include <linux/arm-smccc.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/printk.h>

#define GTS3_BLSP_OWNER_TZ	1	/* hand the bus to TrustZone  */
#define GTS3_BLSP_OWNER_APSS	3	/* hand the bus to Linux (APSS) */

/*
 * SIP, SMC64, owner = SIP (2), function 0x0403. Equals the downstream
 * (SCM_SIP_FNID(4,3) | SMC64_MASK) = 0x42000403.
 */
#define GTS3_BLSP_OWNER_FN_ID						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_64,		\
			   ARM_SMCCC_OWNER_SIP, 0x0403)

/* downstream SCM_ARGS(2) with both args SCM_VAL: num=2, no RW flags */
#define GTS3_BLSP_OWNER_ARGINFO	2

/* downstream retry signals (drivers/soc/qcom/scm.c:48,50) */
#define GTS3_SCM_INTERRUPTED	1L
#define GTS3_SCM_V2_EBUSY	(-12L)
#define GTS3_EBUSY_RETRIES	20
#define GTS3_EBUSY_WAIT_MS	30

/*
 * BLSP QUP index (args[0]). Samsung's GPL touch drivers document the range as
 * "BLSPID (1 - 12)" and pass adapter->nr - 1 (sec_ts.c:137-139,
 * stm/fts_ts.c:149-150). For BLSP1 QUP6 that is 5 if TrustZone wants the
 * 0-based nr-1 value, or 6 if it wants the raw 1-based BLSPID. Default 5; the
 * caller tries 6 next. No other value is justified, and on this tablet the
 * other in-use QUPs sit at the neighbouring indices (see docs/fingerprint.md).
 */
static u32 qup = 5;
module_param(qup, uint, 0644);
MODULE_PARM_DESC(qup, "BLSP QUP index for the ownership call (5=QUP6 0-based, 6=1-based)");

static long gts3_last_ret = -ENODATA;
static struct dentry *gts3_dir;

/* Issue the ownership SMC. owner is GTS3_BLSP_OWNER_TZ or _APSS. */
static long gts3_blsp_set_owner(u32 blsp_index, u32 owner)
{
	struct arm_smccc_res res;
	int tries = 0;

	do {
		arm_smccc_smc(GTS3_BLSP_OWNER_FN_ID, GTS3_BLSP_OWNER_ARGINFO,
			      blsp_index, owner, 0, 0, 0, 0, &res);

		if ((long)res.a0 == GTS3_SCM_V2_EBUSY) {
			if (++tries > GTS3_EBUSY_RETRIES)
				break;
			msleep(GTS3_EBUSY_WAIT_MS);
			continue;
		}
	} while ((long)res.a0 == GTS3_SCM_INTERRUPTED);

	pr_info("gts3-blsp-owner: qup=%u owner=%u -> tz_ret=%ld (a1=%lx)\n",
		blsp_index, owner, (long)res.a0, res.a1);
	return (long)res.a0;
}

static ssize_t gts3_trigger_read(struct file *f, char __user *ubuf,
				 size_t len, loff_t *off)
{
	char buf[64];
	int n;

	n = scnprintf(buf, sizeof(buf), "qup=%u last_ret=%ld\n",
		      qup, gts3_last_ret);
	return simple_read_from_buffer(ubuf, len, off, buf, n);
}

static ssize_t gts3_trigger_write(struct file *f, const char __user *ubuf,
				  size_t len, loff_t *off)
{
	char buf[16];
	u32 owner;

	if (len == 0 || len >= sizeof(buf))
		return -EINVAL;
	if (copy_from_user(buf, ubuf, len))
		return -EFAULT;
	buf[len] = '\0';

	if (kstrtou32(strim(buf), 0, &owner))
		return -EINVAL;
	if (owner != GTS3_BLSP_OWNER_TZ && owner != GTS3_BLSP_OWNER_APSS)
		return -EINVAL;

	gts3_last_ret = gts3_blsp_set_owner(qup, owner);
	return len;
}

static const struct file_operations gts3_trigger_fops = {
	.owner = THIS_MODULE,
	.read = gts3_trigger_read,
	.write = gts3_trigger_write,
	.llseek = noop_llseek,
};

static int __init gts3_blsp_owner_init(void)
{
	gts3_dir = debugfs_create_dir("gts3_blsp_owner", NULL);
	if (IS_ERR(gts3_dir))
		return PTR_ERR(gts3_dir);

	/*
	 * write 3 to hand QUP to Linux (APSS), 1 to hand it back to TZ;
	 * read it back for "qup=<n> last_ret=<tz return code>"
	 */
	debugfs_create_file("owner", 0600, gts3_dir, NULL, &gts3_trigger_fops);
	/* which QUP index the next write will act on */
	debugfs_create_u32("qup", 0644, gts3_dir, &qup);

	pr_info("gts3-blsp-owner: loaded, idle. "
		"echo 3 > /sys/kernel/debug/gts3_blsp_owner/owner to try APSS\n");
	return 0;
}

static void __exit gts3_blsp_owner_exit(void)
{
	debugfs_remove_recursive(gts3_dir);
}

module_init(gts3_blsp_owner_init);
module_exit(gts3_blsp_owner_exit);

MODULE_DESCRIPTION("Galaxy Tab S3 BLSP QUP ownership test aid (TZ SIP call)");
MODULE_AUTHOR("Jayden Milne <linux@jayd.ml>");
MODULE_LICENSE("GPL");
