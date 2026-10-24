// SPDX-License-Identifier: GPL-2.0+
/*
 * SDHCI on Broadcom Kona mobile SoCs (BCM281xx, BCM21664, BCM23550).
 *
 */

#include <config.h>
#include <dm.h>
#include <malloc.h>
#include <sdhci.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <asm/kona-common/clk.h>

#define SDHCI_CORECTRL_OFFSET		0x00008000
#define SDHCI_CORECTRL_EN		0x01
#define SDHCI_CORECTRL_RESET		0x02

#define SDHCI_CORESTAT_OFFSET		0x00008004
#define SDHCI_CORESTAT_CD_SW		0x01

#define SDHCI_COREIMR_OFFSET		0x00008008
#define SDHCI_COREIMR_IP		0x01

struct kona_sdhci_plat {
	struct mmc_config cfg;
	struct mmc mmc;
};

static int init_kona_mmc_core(struct sdhci_host *host)
{
	unsigned int mask;
	unsigned int timeout;

	if (sdhci_readb(host, SDHCI_SOFTWARE_RESET) & SDHCI_RESET_ALL) {
		printf("%s: sd host controller reset error\n", __func__);
		return -EBUSY;
	}

	/* For kona a hardware reset before anything else. */
	mask = sdhci_readl(host, SDHCI_CORECTRL_OFFSET) | SDHCI_CORECTRL_RESET;
	sdhci_writel(host, mask, SDHCI_CORECTRL_OFFSET);

	/* Wait max 100 ms */
	timeout = 1000;
	do {
		if (timeout == 0) {
			printf("%s: reset timeout error\n", __func__);
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(100);
	} while (0 ==
		 (sdhci_readl(host, SDHCI_CORECTRL_OFFSET) &
		  SDHCI_CORECTRL_RESET));

	/* Clear the reset bit. */
	mask = mask & ~SDHCI_CORECTRL_RESET;
	sdhci_writel(host, mask, SDHCI_CORECTRL_OFFSET);

	/* Enable AHB clock */
	mask = sdhci_readl(host, SDHCI_CORECTRL_OFFSET);
	sdhci_writel(host, mask | SDHCI_CORECTRL_EN, SDHCI_CORECTRL_OFFSET);

	/* Enable interrupts */
	sdhci_writel(host, SDHCI_COREIMR_IP, SDHCI_COREIMR_OFFSET);

	/* Make sure Card is detected in controller */
	mask = sdhci_readl(host, SDHCI_CORESTAT_OFFSET);
	sdhci_writel(host, mask | SDHCI_CORESTAT_CD_SW, SDHCI_CORESTAT_OFFSET);

	/* Wait max 100 ms */
	timeout = 1000;
	while (!(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT)) {
		if (timeout == 0) {
			printf("%s: CARD DETECT timeout error\n", __func__);
			return -ETIMEDOUT;
		}
		timeout--;
		udelay(100);
	}
	return 0;
}

static int kona_sdhci_probe(struct udevice *dev)
{
	struct mmc_uclass_priv *upriv = dev_get_uclass_priv(dev);
	struct kona_sdhci_plat *plat = dev_get_plat(dev);
	struct sdhci_host *host = dev_get_priv(dev);
	u32 max_clk;
	int ret;

	host->name = dev->name;
	host->ioaddr = dev_read_addr_ptr(dev);
	if (!host->ioaddr)
		return -EINVAL;

	/*
	 * The DT's max-frequency specifies the desired clock rate. clk_sdio_enable()
	 * returns the rate actually achieved by the divider, which is then stored
	 * as max_clk.
	
	 * Previously, both steps used printf() for logging because accessing a
	 * controller whose AHB gate is still disabled can stall on the initial
	 * register access. The bus stops without generating an abort or timing out,
	 * so the remaining log message indicated which step caused the failure.
	 * Keep these messages as debug() calls rather than removing them entirely:
	 * since the failure is silent by nature, adding #define DEBUG at the top
	 * of this file provides the quickest way to diagnose it.
	 */
	debug("kona-sdhci: %s at %p: enabling clocks\n", dev->name,
	      host->ioaddr);
	ret = clk_sdio_enable(host->ioaddr,
			      dev_read_u32_default(dev, "max-frequency",
						   400000),
			      &max_clk);
	if (ret)
		return ret;

	debug("kona-sdhci: %s: clocks ok (%u Hz), resetting core\n",
	      dev->name, max_clk);
	ret = init_kona_mmc_core(host);
	if (ret) {
		printf("kona-sdhci: %s: core init failed: %d\n",
		       dev->name, ret);
		return ret;
	}

	host->max_clk = max_clk;
	host->mmc = &plat->mmc;
	host->mmc->dev = dev;

	ret = sdhci_setup_cfg(&plat->cfg, host, max_clk, 400000);
	if (ret)
		return ret;

	host->mmc->priv = host;
	upriv->mmc = host->mmc;

	return sdhci_probe(dev);
}

static int kona_sdhci_bind(struct udevice *dev)
{
	struct kona_sdhci_plat *plat = dev_get_plat(dev);

	return sdhci_bind(dev, &plat->mmc, &plat->cfg);
}

static const struct udevice_id kona_sdhci_match[] = {
	{ .compatible = "brcm,kona-sdhci" },
	{ }
};

U_BOOT_DRIVER(kona_sdhci) = {
	.name		= "kona-sdhci",
	.id		= UCLASS_MMC,
	.of_match	= kona_sdhci_match,
	.ops		= &sdhci_ops,
	.bind		= kona_sdhci_bind,
	.probe		= kona_sdhci_probe,
	.priv_auto	= sizeof(struct sdhci_host),
	.plat_auto	= sizeof(struct kona_sdhci_plat),
};
