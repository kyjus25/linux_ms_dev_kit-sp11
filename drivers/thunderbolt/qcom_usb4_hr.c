// SPDX-License-Identifier: GPL-2.0-only
/*
 * Qualcomm X1E USB4 host-router bring-up scaffold.
 *
 * By default this only validates that the DT describes the complete
 * host-router resource set.  The activation path is explicitly opt-in while
 * the firmware and Qualcomm-specific initialization sequence are incomplete.
 */

#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/string.h>

#include "nhi.h"
#include "tb.h"
#include "tb_regs.h"

static const char * const qcom_usb4_hr_regs[] = {
	"router", "router_config", "tmu_config", "port_group",
	"sideband", "uc_ram", "uc_per", "uc_mbox", "nhi", "cfg",
	"debug", "usbap_config", "pcieap_config", "dpap0_aux",
	"dpap0_config", "dpap1_aux", "dpap1_config",
};

static const char * const qcom_usb4_hr_clocks[] = {
	"sys", "tmu", "ahb", "axi", "master", "phy_rx0", "phy_rx1",
	"sb", "dp0", "dp1",
};

static const char * const qcom_usb4_hr_resets[] = {
	"core", "phy_nocsr", "sys", "rx0", "rx1", "usb_pipe",
	"pcie_pipe", "tmu", "sideband_iface", "hia_master", "ahb",
	"dp0", "dp1",
};

struct qcom_usb4_hr {
	void __iomem *regs[ARRAY_SIZE(qcom_usb4_hr_regs)];
	struct clk_bulk_data clocks[ARRAY_SIZE(qcom_usb4_hr_clocks)];
	struct reset_control_bulk_data resets[ARRAY_SIZE(qcom_usb4_hr_resets)];
	struct phy *usb4_phy;
	struct tb_nhi nhi;
	int ring_irq;
	int fw_irq;
	bool activated;
};

static inline struct qcom_usb4_hr *qcom_usb4_hr_from_nhi(struct tb_nhi *nhi)
{
	return container_of(nhi, struct qcom_usb4_hr, nhi);
}

static irqreturn_t qcom_usb4_hr_fw_irq(int irq, void *data);
static irqreturn_t qcom_usb4_hr_fw_thread(int irq, void *data);

static int qcom_usb4_hr_init_interrupts(struct tb_nhi *nhi)
{
	struct qcom_usb4_hr *hr = qcom_usb4_hr_from_nhi(nhi);
	int ret;

	INIT_WORK(&nhi->interrupt_work, nhi_interrupt_work);
	ret = devm_request_irq(nhi->dev, hr->ring_irq, nhi_msi,
			       IRQF_NO_SUSPEND, "thunderbolt", nhi);
	if (ret)
		return ret;

	/*
	 * The UC's event/notification line; the common NHI code has no use
	 * for it yet, but log activity so connect events become visible.
	 */
	ret = devm_request_threaded_irq(nhi->dev, hr->fw_irq,
					qcom_usb4_hr_fw_irq,
					qcom_usb4_hr_fw_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					"thunderbolt-fw", hr);
	if (ret)
		dev_warn(nhi->dev, "fw_irq request failed: %pe\n", ERR_PTR(ret));
	return 0;
}

static bool qcom_usb4_hr_is_present(struct tb_nhi *nhi)
{
	return !nhi->going_away;
}

static const struct tb_nhi_ops qcom_usb4_hr_nhi_ops = {
	.is_present = qcom_usb4_hr_is_present,
	.init_interrupts = qcom_usb4_hr_init_interrupts,
};

static int qcom_usb4_hr_nhi_preflight(struct qcom_usb4_hr *hr,
					      struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	/*
	 * The common NHI code is ready for platform users, but Qualcomm's
	 * router-specific firmware upload and power-on sequence is not. Keep
	 * this check explicitly opt-in so a DT node cannot accidentally turn
	 * the resource validator into a boot-critical driver.
	 */
	if (!device_property_read_bool(dev, "qcom,nhi-preflight") &&
	    !device_property_read_bool(dev, "qcom,nhi-activate"))
		return 0;

	hr->ring_irq = platform_get_irq_byname(pdev, "ring");
	if (hr->ring_irq < 0)
		return dev_err_probe(dev, hr->ring_irq,
				     "missing combined NHI ring interrupt\n");

	hr->fw_irq = platform_get_irq_byname(pdev, "fw");
	if (hr->fw_irq < 0)
		return dev_err_probe(dev, hr->fw_irq,
				     "missing NHI firmware interrupt\n");

	hr->nhi.dev = dev;
	hr->nhi.ops = &qcom_usb4_hr_nhi_ops;
	hr->nhi.iobase = hr->regs[8];
	hr->usb4_phy = devm_phy_get(dev, "usb4");
	if (IS_ERR(hr->usb4_phy))
		return dev_err_probe(dev, PTR_ERR(hr->usb4_phy),
				     "USB4 PHY is not available\n");

	dev_info(dev,
		 "NHI preflight passed: aperture=%p ring_irq=%d fw_irq=%d; activation=%s\n",
		 hr->nhi.iobase, hr->ring_irq, hr->fw_irq,
		 device_property_read_bool(dev, "qcom,nhi-activate") ?
		 "requested" : "disabled");
	return 0;
}

static irqreturn_t qcom_usb4_hr_fw_irq(int irq, void *data)
{
	struct qcom_usb4_hr *hr = data;

	dev_dbg(hr->nhi.dev, "fw_irq fired (irq %d)\n", irq);
	return IRQ_WAKE_THREAD;
}

static irqreturn_t qcom_usb4_hr_fw_thread(int irq, void *data)
{
	struct qcom_usb4_hr *hr = data;

	dev_info(hr->nhi.dev, "fw_irq thread: uc_per status %08x %08x %08x\n",
		 readl(hr->regs[6] + 0x00), readl(hr->regs[6] + 0x04),
		 readl(hr->regs[6] + 0x1000));
	return IRQ_HANDLED;
}

static u32 qcom_usb4_hr_rmw(void __iomem *addr, u32 value, u32 mask)
{
	u32 val = readl(addr);

	val &= ~mask;
	val |= value & mask;
	writel(val, addr);
	return val;
}

static int qcom_usb4_hr_uc_bringup(struct qcom_usb4_hr *hr)
{
	static const char * const fw_name =
		"qcom/x1e80100/microsoft/Denali/sp11-usb4-uc-fw.bin";
	void __iomem *uc_ctl = hr->regs[6] + 0x1000; /* uc_per + 0x1000 */
	const struct firmware *fw;
	const __le32 *p, *end;
	unsigned int i;
	u32 val;
	int ret;

	/*
	 * Sequence reverse-engineered from the Windows QcUsb4Filter driver
	 * (static analysis only; addresses and order documented in the
	 * experiment README). The UC implements the NHI ring protocol, so
	 * once it is running the common Thunderbolt stack can talk to it.
	 */
	val = readl(uc_ctl);
	if (val & BIT(0)) {
		dev_info(hr->nhi.dev, "USB4 UC already running (warm start)\n");
		return 0;
	}

	ret = request_firmware(&fw, fw_name, hr->nhi.dev);
	if (ret)
		return dev_err_probe(hr->nhi.dev, ret,
				     "USB4 UC firmware unavailable\n");

	/* Halt the UC, then stream the {target, count, words[]} segments. */
	writel(0, uc_ctl);

	p = (const __le32 *)fw->data;
	end = p + fw->size / sizeof(__le32);
	while (p + 2 <= end) {
		u32 target = le32_to_cpu(p[0]);
		u32 count = le32_to_cpu(p[1]);

		p += 2;
		if (!count || count > 0x4000 ||
		    target > SZ_64K || p + count > end ||
		    target + count * sizeof(u32) > (56 * SZ_1K)) {
			dev_err(hr->nhi.dev, "bad UC firmware segment\n");
			release_firmware(fw);
			return -EINVAL;
		}
		for (i = 0; i < count; i++)
			writel(le32_to_cpu(p[i]),
			       hr->regs[5] + target + i * sizeof(u32));
		p += count;
	}
	dev_info(hr->nhi.dev, "USB4 UC firmware loaded (%zu bytes)\n",
		 fw->size);
	release_firmware(fw);

	/*
	 * The QMP driver leaves the USB4 AON clamp engaged (com_init sets it
	 * and only the autonomous-mode path clears it, which never runs for
	 * the router). With the clamp set the PHY pins are isolated and the
	 * UC cannot see the partner. Release it: the router owns the pins.
	 */
	{
		struct device_node *np = hr->usb4_phy->dev.of_node;
		void __iomem *phy_base = of_iomap(np, 0);

		if (phy_base) {
			void __iomem *clmp = phy_base + 0x104;
			u32 v = readl(clmp);

			writel(v & ~0x01010101u, clmp);
			dev_info(hr->nhi.dev,
				 "USB4 AON clamp: %#x -> %#x\n",
				 v, readl(clmp));
			iounmap(phy_base);
		} else {
			dev_warn(hr->nhi.dev,
				 "could not map PHY to release clamp\n");
		}
	}

	/* Clear the two gates, then release the UC. */
	qcom_usb4_hr_rmw(hr->regs[3] + 0x64, 0, BIT(6));   /* port_group */
	qcom_usb4_hr_rmw(hr->regs[1] + 0x18, 0, BIT(24));  /* router_config */
	qcom_usb4_hr_rmw(uc_ctl, 1, BIT(0));               /* GO */

	/* Wait for the UC to report ready (router + 0x18, bit 24). */
	ret = readl_poll_timeout(hr->regs[0] + 0x18, val,
				 val & BIT(24), 5000, 10 * USEC_PER_SEC);
	if (ret)
		return dev_err_probe(hr->nhi.dev, ret,
				     "USB4 UC did not report ready\n");

	dev_info(hr->nhi.dev, "USB4 UC ready\n");
	return 0;
}

static int qcom_usb4_hr_activate(struct qcom_usb4_hr *hr,
					struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	/*
	 * This is deliberately separate from the resource preflight.  The
	 * common NHI probe resets and touches the router, so only an explicitly
	 * named DT experiment may reach it.  No Qualcomm mailbox or firmware
	 * upload sequence is inferred here.
	 */
	if (!device_property_read_bool(dev, "qcom,nhi-activate"))
		return 0;

	ret = clk_bulk_prepare_enable(ARRAY_SIZE(hr->clocks), hr->clocks);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to enable host-router clocks\n");

	ret = reset_control_bulk_deassert(ARRAY_SIZE(hr->resets), hr->resets);
	if (ret)
		goto err_disable_clocks;

	ret = phy_set_mode_ext(hr->usb4_phy, PHY_MODE_TBT, PHY_SUBMODE_TBT3);
	if (ret)
		goto err_assert_resets;

	ret = phy_init(hr->usb4_phy);
	if (ret)
		goto err_assert_resets;

	ret = qcom_usb4_hr_uc_bringup(hr);
	if (ret)
		goto err_exit_phy;

	ret = nhi_probe(&hr->nhi);
	if (ret)
		goto err_exit_phy;

	hr->activated = true;
	dev_info(dev,
		 "experimental Qualcomm USB4 NHI activated with UC firmware\n");
	return 0;

err_exit_phy:
	phy_exit(hr->usb4_phy);
err_assert_resets:
	reset_control_bulk_assert(ARRAY_SIZE(hr->resets), hr->resets);
err_disable_clocks:
	clk_bulk_disable_unprepare(ARRAY_SIZE(hr->clocks), hr->clocks);
	return dev_err_probe(dev, ret, "host-router activation failed\n");
}

static int qcom_usb4_hr_find_tb_domain(struct device *child, const void *data)
{
	return child->bus == &tb_bus_type;
}

/*
 * Writes issue a raw config-space read to the host router, testing whether
 * the UC's command processor still answers long after the boot-time
 * handshake. Read ROUTER_CS_1 (router vendor ID) and report latency.
 */
static ssize_t uc_ping_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct device *tb_dev;
	struct tb *tb;
	ktime_t t0;
	int i;
	s64 us;
	u32 val = 0;
	int ret;

	tb_dev = device_find_child(dev, NULL, qcom_usb4_hr_find_tb_domain);
	if (!tb_dev)
		return -ENODEV;
	tb = container_of(tb_dev, struct tb, dev);
	put_device(tb_dev);
	if (!tb->root_switch)
		return -ENODEV;

	t0 = ktime_get();
	ret = tb_sw_read(tb->root_switch, &val, TB_CFG_SWITCH, ROUTER_CS_1, 1);
	us = ktime_us_delta(ktime_get(), t0);

	dev_info(dev, "uc_ping: ret=%d val=%#x latency=%lld us\n",
		 ret, val, us);

	for (i = 0; i <= tb->root_switch->config.max_port_number; i++) {
		struct tb_port *port = &tb->root_switch->ports[i];
		u32 cs18 = 0, cs19 = 0;

		if (!port->cap_usb4)
			continue;
		tb_port_read(port, &cs18, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_18, 1);
		tb_port_read(port, &cs19, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_19, 1);
		dev_info(dev,
			 "uc_ping: port%d cap_usb4=%#x CS18=%#x CS19=%#x TIP=%d CPS=%d BE=%d\n",
			 i, port->cap_usb4, cs18, cs19,
			 !!(cs18 & PORT_CS_18_TIP), !!(cs18 & PORT_CS_18_CPS),
			 !!(cs18 & PORT_CS_18_BE));
	}
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(uc_ping);

static int qcom_usb4_hr_probe(struct platform_device *pdev)
{
	struct qcom_usb4_hr *hr;
	int i, ret;

	hr = devm_kzalloc(&pdev->dev, sizeof(*hr), GFP_KERNEL);
	if (!hr)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(qcom_usb4_hr_regs); i++) {
		hr->regs[i] = devm_platform_ioremap_resource_byname(pdev,
							 qcom_usb4_hr_regs[i]);
		if (IS_ERR(hr->regs[i]))
			return dev_err_probe(&pdev->dev, PTR_ERR(hr->regs[i]),
					     "missing MMIO resource %s\n",
					     qcom_usb4_hr_regs[i]);
	}

	for (i = 0; i < ARRAY_SIZE(qcom_usb4_hr_clocks); i++)
		hr->clocks[i].id = qcom_usb4_hr_clocks[i];

	ret = devm_clk_bulk_get(&pdev->dev, ARRAY_SIZE(hr->clocks),
					hr->clocks);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "missing host-router clocks\n");

	for (i = 0; i < ARRAY_SIZE(qcom_usb4_hr_resets); i++)
		hr->resets[i].id = qcom_usb4_hr_resets[i];

	/* Identify reset ownership failures individually while bringing this up. */
	for (i = 0; i < ARRAY_SIZE(hr->resets); i++) {
		/* The QMP PHY owns this reset on the DT wiring used here. */
		if (!strcmp(hr->resets[i].id, "phy_nocsr"))
			hr->resets[i].rstc = devm_reset_control_get_optional_shared(
				&pdev->dev, hr->resets[i].id);
		else
			hr->resets[i].rstc = devm_reset_control_get_shared(&pdev->dev,
								 hr->resets[i].id);
		if (IS_ERR(hr->resets[i].rstc))
			return dev_err_probe(&pdev->dev, PTR_ERR(hr->resets[i].rstc),
					     "reset %s unavailable\n",
					     hr->resets[i].id);
	}

	platform_set_drvdata(pdev, hr);

	ret = qcom_usb4_hr_nhi_preflight(hr, pdev);
	if (ret)
		return ret;

	ret = qcom_usb4_hr_activate(hr, pdev);
	if (ret)
		return ret;

	ret = device_create_file(&pdev->dev, &dev_attr_uc_ping);

	dev_info(&pdev->dev,
		 hr->activated ? "resource validation passed; host-router NHI is active\n" :
		 "resource validation passed; host-router hardware access is disabled\n");
	return 0;
}



static const struct of_device_id qcom_usb4_hr_of_match[] = {
	{ .compatible = "qcom,x1e80100-usb4-hr" },
	{ .compatible = "qcom,usb4-hr" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_usb4_hr_of_match);

static struct platform_driver qcom_usb4_hr_driver = {
	.probe = qcom_usb4_hr_probe,
	.driver = {
		.name = "qcom-usb4-hr",
		.of_match_table = qcom_usb4_hr_of_match,
	},
};
module_platform_driver(qcom_usb4_hr_driver);

MODULE_DESCRIPTION("Qualcomm X1E USB4 host-router bring-up scaffold");
MODULE_LICENSE("GPL");
