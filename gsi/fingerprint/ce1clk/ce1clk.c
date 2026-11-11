// SPDX-License-Identifier: GPL-2.0
/*
 * Test: keep the crypto engine CE1's clocks on (RPM CE1, GCC CE1 AHB/AXI), as
 * downstream qseecom does while TrustZone apps run, to see whether Samsung's
 * fingerprint trustlet needs them. Unload to switch them off.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <dt-bindings/clock/qcom,gcc-msm8996.h>
#include <dt-bindings/clock/qcom,rpmcc.h>

static struct clk *clks[3];

static int ce1_get(int i, const char *compat, u32 id)
{
	struct of_phandle_args a = { .args_count = 1, .args = { id } };
	int ret;

	a.np = of_find_compatible_node(NULL, NULL, compat);
	if (!a.np)
		return -ENODEV;
	clks[i] = of_clk_get_from_provider(&a);
	of_node_put(a.np);
	if (IS_ERR(clks[i])) {
		ret = PTR_ERR(clks[i]);
		clks[i] = NULL;
		return ret;
	}
	ret = clk_prepare_enable(clks[i]);
	if (ret) {
		clk_put(clks[i]);
		clks[i] = NULL;
	}
	return ret;
}

static int __init ce1clk_init(void)
{
	int r0 = ce1_get(0, "qcom,rpmcc-msm8996", RPM_SMD_CE1_CLK);
	int r1 = ce1_get(1, "qcom,gcc-msm8996", GCC_CE1_AHB_CLK);
	int r2 = ce1_get(2, "qcom,gcc-msm8996", GCC_CE1_AXI_CLK);

	pr_info("ce1clk: rpm ce1 %d (%lu Hz), ahb %d, axi %d\n", r0,
		clks[0] ? clk_get_rate(clks[0]) : 0, r1, r2);
	return 0;
}

static void __exit ce1clk_exit(void)
{
	int i;

	for (i = 0; i < 3; i++)
		if (clks[i]) {
			clk_disable_unprepare(clks[i]);
			clk_put(clks[i]);
		}
}

module_init(ce1clk_init);
module_exit(ce1clk_exit);
MODULE_LICENSE("GPL");
