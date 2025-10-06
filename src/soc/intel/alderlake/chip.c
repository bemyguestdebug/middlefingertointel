/* SPDX-License-Identifier: GPL-2.0-only */

#include <device/device.h>
#include <device/pci.h>
#include <fsp/api.h>
#include <fsp/util.h>
#include <gpio.h>
#include <intelblocks/acpi.h>
#include <intelblocks/cfg.h>
#include <intelblocks/cse.h>
#include <intelblocks/irq.h>
#include <intelblocks/itss.h>
#include <intelblocks/pcie_rp.h>
#include <intelblocks/systemagent.h>
#include <intelblocks/xdci.h>
#include <soc/hsphy.h>
#include <soc/intel/common/vbt.h>
#include <soc/p2sb.h>
#include <soc/pci_devs.h>
#include <soc/pcie.h>
#include <soc/ramstage.h>
#include <soc/soc_chip.h>
#include <static.h>

#if CONFIG(HAVE_ACPI_TABLES)
const char *soc_acpi_name(const struct device *dev)
{
	if (!dev) {
		printk(BIOS_ERR, "soc_acpi_name: device pointer is NULL\n");
		return NULL;
	}

	if (dev->path.type == DEVICE_PATH_DOMAIN) {
		return "PCI0";
	}

	if (dev->path.type == DEVICE_PATH_USB) {
		switch (dev->path.usb.port_type) {
		case 0:
			/* Root Hub */
			return "RHUB";

		case 2:
			/* USB2 ports */
			switch (dev->path.usb.port_id) {
			case 0: return "HS01";
			case 1: return "HS02";
			case 2: return "HS03";
			case 3: return "HS04";
			case 4: return "HS05";
			case 5: return "HS06";
			case 6: return "HS07";
			case 7: return "HS08";
			case 8: return "HS09";
			case 9: return "HS10";
			case 10: return "HS11";
			case 11: return "HS12";
			case 12: return "HS13";
			case 13: return "HS14";
			default:
				printk(BIOS_WARN, "Unknown USB2 port_id %u\n", dev->path.usb.port_id);
				return NULL;
			}
			break;

		case 3:
			/* USB3 ports */
			switch (dev->path.usb.port_id) {
			case 0: return "SS01";
			case 1: return "SS02";
			case 2: return "SS03";
			case 3: return "SS04";
			case 4: return "SS05";
			case 5: return "SS06";
			case 6: return "SS07";
			case 7: return "SS08";
			case 8: return "SS09";
			case 9: return "SS10";
			default:
				printk(BIOS_WARN, "Unknown USB3 port_id %u\n", dev->path.usb.port_id);
				return NULL;
			}
			break;

		default:
			printk(BIOS_WARN, "Unknown USB port_type %u\n", dev->path.usb.port_type);
			return NULL;
		}

		return NULL; /* Fallback */
	}

	if (dev->path.type != DEVICE_PATH_PCI) {
		return NULL;
	}

	switch (dev->path.pci.devfn) {
	case SA_DEVFN_ROOT:
		return "MCHC";

#if CONFIG(SOC_INTEL_ALDERLAKE_PCH_S)
	case SA_DEVFN_CPU_PCIE1_0:
		return "PEG1";
	case SA_DEVFN_CPU_PCIE1_1:
		return "PEG2";
	case SA_DEVFN_CPU_PCIE6_0:
		return "PEG0";
#else
	case SA_DEVFN_CPU_PCIE1_0:
		return "PEG2";
	case SA_DEVFN_CPU_PCIE6_0:
		return "PEG0";
	case SA_DEVFN_CPU_PCIE6_2:
		return "PEG1";
#endif

	case SA_DEVFN_IGD:
		return "GFX0";

	case SA_DEVFN_TCSS_XHCI:
		return "TXHC";

	case SA_DEVFN_TCSS_XDCI:
		return "TXDC";

	case SA_DEVFN_TCSS_DMA0:
		return "TDM0";

	case SA_DEVFN_TCSS_DMA1:
		return "TDM1";

	case SA_DEVFN_TBT0:
		return "TRP0";

	case SA_DEVFN_TBT1:
		return "TRP1";

	case SA_DEVFN_TBT2:
		return "TRP2";

	case SA_DEVFN_TBT3:
		return "TRP3";

	case SA_DEVFN_IPU:
		return "IPU0";

	case SA_DEVFN_GNA:
		return "GNA";

	case SA_DEVFN_DPTF:
		return "TCPU";

	case PCH_DEVFN_ISH:
		return "ISHB";

	case PCH_DEVFN_XHCI:
		return "XHCI";

	case PCH_DEVFN_I2C0:
		return "I2C0";

	case PCH_DEVFN_I2C1:
		return "I2C1";

	case PCH_DEVFN_I2C2:
		return "I2C2";

	case PCH_DEVFN_I2C3:
		return "I2C3";

	case PCH_DEVFN_I2C4:
		return "I2C4";

	case PCH_DEVFN_I2C5:
		return "I2C5";

	case PCH_DEVFN_I2C6:
		return "I2C6";

	case PCH_DEVFN_I2C7:
		return "I2C7";

	case PCH_DEVFN_SATA:
		return "SATA";

	case PCH_DEVFN_PCIE1:
		return "RP01";

	case PCH_DEVFN_PCIE2:
		return "RP02";

	case PCH_DEVFN_PCIE3:
		return "RP03";

	case PCH_DEVFN_PCIE4:
		return "RP04";

	case PCH_DEVFN_PCIE5:
		return "RP05";

	case PCH_DEVFN_PCIE6:
		return "RP06";

	case PCH_DEVFN_PCIE7:
		return "RP07";

	case PCH_DEVFN_PCIE8:
		return "RP08";

	case PCH_DEVFN_PCIE9:
		return "RP09";

	case PCH_DEVFN_PCIE10:
		return "RP10";

	case PCH_DEVFN_PCIE11:
		return "RP11";

	case PCH_DEVFN_PCIE12:
		return "RP12";

	case PCH_DEVFN_PCIE13:
		return "RP13";

	case PCH_DEVFN_PCIE14:
		return "RP14";

	case PCH_DEVFN_PCIE15:
		return "RP15";

	case PCH_DEVFN_PCIE16:
		return "RP16";

	case PCH_DEVFN_PCIE17:
		return "RP17";

	case PCH_DEVFN_PCIE18:
		return "RP18";

	case PCH_DEVFN_PCIE19:
		return "RP19";

	case PCH_DEVFN_PCIE20:
		return "RP20";

	case PCH_DEVFN_PCIE21:
		return "RP21";

	case PCH_DEVFN_PCIE22:
		return "RP22";

	case PCH_DEVFN_PCIE23:
		return "RP23";

	case PCH_DEVFN_PCIE24:
		return "RP24";

#if CONFIG(SOC_INTEL_ALDERLAKE_PCH_S)
	/* Avoid conflicts with PCH-N eMMC */
	case PCH_DEVFN_PCIE25:
		return "RP25";

	case PCH_DEVFN_PCIE26:
		return "RP26";

	case PCH_DEVFN_PCIE27:
		return "RP27";

	case PCH_DEVFN_PCIE28:
		return "RP28";
#endif

	case PCH_DEVFN_PMC:
		return "PMC";

	case PCH_DEVFN_UART0:
		return "UAR0";

	case PCH_DEVFN_UART1:
		return "UAR1";

	case PCH_DEVFN_UART2:
		return "UAR2";

	case PCH_DEVFN_GSPI0:
		return "SPI0";

	case PCH_DEVFN_GSPI1:
		return "SPI1";

	case PCH_DEVFN_GSPI2:
		return "SPI2";

	case PCH_DEVFN_GSPI3:
		return "SPI3";

	/* Keeping ACPI device name coherent with ec.asl */
	case PCH_DEVFN_ESPI:
		return "LPCB";

	case PCH_DEVFN_HDA:
		return "HDAS";

	case PCH_DEVFN_SMBUS:
		return "SBUS";

	case PCH_DEVFN_GBE:
		return "GLAN";

	case PCH_DEVFN_SRAM:
		return "SRAM";

	case PCH_DEVFN_SPI:
		return "FSPI";

	case PCH_DEVFN_CSE:
		return "HECI";

#if CONFIG(SOC_INTEL_ALDERLAKE_PCH_N)
	case PCH_DEVFN_EMMC:
		return "EMMC";
#endif

	default:
		printk(BIOS_WARN, "Unknown PCI device devfn 0x%02x\n", dev->path.pci.devfn);
		return NULL;
	}

	return NULL; /* Defensive fallback */
}
#endif /* CONFIG(HAVE_ACPI_TABLES) */

#if CONFIG(SOC_INTEL_STORE_ISH_FW_VERSION)
/*
 * SoC override API to identify if ISH Firmware existed inside CSE FPT.
 *
 * Identifying the ISH enabled device is required to conclude that the ISH
 * partition also is available (because ISH may be default enabled for non-UFS
 * platforms as well starting with Alder Lake).
 */
bool soc_is_ish_partition_enabled(void)
{
	struct device *ish = pcidev_path_on_root(PCH_DEVFN_ISH);
	if (!ish) {
		printk(BIOS_DEBUG, "ISH device not found on PCI root\n");
		return false;
	}

	uint16_t ish_pci_id = pci_read_config16(ish, PCI_DEVICE_ID);

	if (ish_pci_id == 0xFFFF) {
		printk(BIOS_WARN, "ISH PCI device ID invalid (0xFFFF)\n");
		return false;
	}

	return true;
}
#endif /* CONFIG(SOC_INTEL_STORE_ISH_FW_VERSION) */

/* SoC routine to fill GPIO PM mask and value for GPIO_MISCCFG register */
static void soc_fill_gpio_pm_configuration(void)
{
	uint8_t value[TOTAL_GPIO_COMM];
	const config_t *config = config_of_soc();

	if (!config) {
		printk(BIOS_ERR, "soc_fill_gpio_pm_configuration: config_of_soc() returned NULL\n");
		return;
	}

	if (config->gpio_override_pm) {
		memcpy(value, config->gpio_pm, sizeof(value));
	} else {
		memset(value, MISCCFG_GPIO_PM_CONFIG_BITS, sizeof(value));
	}

	gpio_pm_configure(value, TOTAL_GPIO_COMM);
}

void soc_init_pre_device(void *chip_info)
{
	(void)chip_info; /* unused param */

	/* HSPHY FW needs to be loaded before FSP silicon init */
	if (!load_and_init_hsphy()) {
		printk(BIOS_ERR, "Failed to load and initialize HS PHY firmware\n");
	}

	/* Perform silicon specific init. */
	fsp_silicon_init();

	/* Display FIRMWARE_VERSION_INFO_HOB */
	fsp_display_fvi_version_hob();

	soc_fill_gpio_pm_configuration();

	/* Swap enabled PCI ports in device tree if needed. */
	pcie_rp_update_devicetree(get_pch_pcie_rp_table());

	/* Swap enabled TBT root ports in device tree if needed. */
	pcie_rp_update_devicetree(get_tbt_pcie_rp_table());

	/*
	 * Earlier when coreboot used to send EOP at late as possible caused
	 * lag in firmware communication latency. To reduce it, early EOP message
	 * is sent right after PCH initialization based on configuration.
	 */
	if (config_of_soc()->cse_early_eop) {
		cse_send_eop();
	}
}

void cpu_generate_ssdt_acpi(struct device *dev)
{
	if (!dev) {
		printk(BIOS_ERR, "cpu_generate_ssdt_acpi: NULL device pointer\n");
		return;
	}
	acpi_generate_cpu_ssdt(dev);
}

void cpu_program_irq(struct device *dev)
{
	if (!dev) {
		printk(BIOS_ERR, "cpu_program_irq: NULL device pointer\n");
		return;
	}
	intel_irq_program_nonpch(dev);
}

/* PCI domain operations */
static struct pci_domain_ops pci_domain_ops = {
	.acquire_resources = pci_domain_acquire_resources,
	.release_resources = pci_domain_release_resources,
};

/* PCI device operations */
static struct pci_device_ops pci_device_ops = {
	.acquire_resources = pci_dev_acquire_resources,
	.release_resources = pci_dev_release_resources,
};

/* CPU device operations */
static struct cpu_device_ops cpu_device_ops = {
	.generate_ssdt_acpi = cpu_generate_ssdt_acpi,
	.program_irq = cpu_program_irq,
};

int soc_enable(struct device *dev)
{
	if (!dev) {
		printk(BIOS_ERR, "soc_enable: device pointer is NULL\n");
		return -EINVAL;
	}

	switch (dev->path.type) {
	case DEVICE_PATH_PCI:
		if (dev->path.pci.devfn == PCI_DEVFN(0, 0)) {
			dev->ops = &pci_domain_ops;
		} else {
			dev->ops = &pci_device_ops;
		}
		break;

	case DEVICE_PATH_CPU:
		dev->ops = &cpu_device_ops;
		break;

	case DEVICE_PATH_BUS:
		if (dev->bus.type == BUS_TYPE_PCI)
			dev->ops = &pci_domain_ops;
		break;

	case DEVICE_PATH_HIDDEN:
		/* Enable PMC and P2SB hidden devices */
		if (dev->hidden.id == SOC_DEVICE_PMC || dev->hidden.id == SOC_DEVICE_P2SB) {
			dev->flags |= DEVICE_F_ENABLED;
		}
		break;

	case DEVICE_PATH_GPIO:
		/* GPIO enable handled here */
		gpio_enable(dev);
		break;

	default:
		printk(BIOS_WARN, "soc_enable: Unknown device path type %d\n", dev->path.type);
		return -EINVAL;
	}

	return 0;
}

static struct soc_ops soc_intel_alderlake_ops = {
	.name = "Intel Alder Lake",
	.enable = soc_enable,
	.init_pre_device = soc_init_pre_device,
};

DECLARE_SOC_DEVICE(intel_alderlake, &soc_intel_alderlake_ops);
