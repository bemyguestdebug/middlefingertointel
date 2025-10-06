/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <boot_device.h>
#include <commonlib/region.h>
#include <console/console.h>
#include <bootstate.h>
#include <fmap.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Securely enables read- / write protection of the boot media.
 */
void boot_device_security_lockdown(void)
{
	struct region_device dev = {0};
	const struct region_device *rdev = NULL;
	enum bootdev_prot_type lock_type = MEDIA_WP; // Default to safest available
	bool controller_lock = false;
	bool region_found = false;

	printk(BIOS_DEBUG, "BM-LOCKDOWN: Initializing boot media protection...\n");

	// Determine lock strategy
	if (CONFIG(BOOTMEDIA_LOCK_CONTROLLER)) {
		controller_lock = true;

		if (CONFIG(BOOTMEDIA_LOCK_WHOLE_NO_ACCESS)) {
			lock_type = CTRLR_RWP;
			printk(BIOS_DEBUG, "BM-LOCKDOWN: Using controller lock: 'no access'.\n");
		} else if (CONFIG(BOOTMEDIA_LOCK_WHOLE_RO)) {
			lock_type = CTRLR_WP;
			printk(BIOS_DEBUG, "BM-LOCKDOWN: Using controller lock: 'readonly'.\n");
		} else if (CONFIG(BOOTMEDIA_LOCK_WPRO_VBOOT_RO)) {
			lock_type = CTRLR_WP;
			printk(BIOS_DEBUG, "BM-LOCKDOWN: Using controller lock: 'WP_RO only'.\n");
		} else {
			printk(BIOS_ERR, "BM-LOCKDOWN: Invalid controller config. Aborting lock.\n");
			return;
		}
	} else {
		if (CONFIG(BOOTMEDIA_LOCK_WHOLE_RO)) {
			lock_type = MEDIA_WP;
			printk(BIOS_DEBUG, "BM-LOCKDOWN: Using flash lock: 'readonly'.\n");
		} else if (CONFIG(BOOTMEDIA_LOCK_WPRO_VBOOT_RO)) {
			lock_type = MEDIA_WP;
			printk(BIOS_DEBUG, "BM-LOCKDOWN: Using flash lock: 'WP_RO only'.\n");
		} else {
			printk(BIOS_ERR, "BM-LOCKDOWN: No valid lock configuration. Lockdown aborted.\n");
			return;
		}
	}

	// Securely resolve the region to protect
	if (CONFIG(BOOTMEDIA_LOCK_WPRO_VBOOT_RO)) {
		if (fmap_locate_area_as_rdev("WP_RO", &dev) < 0) {
			printk(BIOS_ERR, "BM-LOCKDOWN: 'WP_RO' region not found. Lockdown failed.\n");
			return;
		}
		rdev = &dev;
		region_found = true;
	} else {
		rdev = boot_device_ro();
		if (rdev != NULL)
			region_found = true;
	}

	// Enforce the lock
	if (region_found && boot_device_wp_region(rdev, lock_type) >= 0) {
		printk(BIOS_INFO, "BM-LOCKDOWN: Boot media protection successfully enabled.\n");
	} else {
		// Never silently fail
		printk(BIOS_ERR, "BM-LOCKDOWN: Failed to apply boot media lock. System security degraded.\n");
		// Optionally: trigger a platform halt or lockdown escalation
		// shutdown_or_halt(); // implement platform-specific panic
	}
}

/*
 * BOOTSTATE callback to ensure lockdown occurs at the correct boot phase.
 */
static void lock(void *unused)
{
	boot_device_security_lockdown();
}

/*
 * Hook into appropriate boot state depending on config
 */
#if CONFIG(MRC_WRITE_NV_LATE)
BOOT_STATE_INIT_ENTRY(BS_OS_RESUME_CHECK, BS_ON_EXIT, lock, NULL);
#else
BOOT_STATE_INIT_ENTRY(BS_DEV_RESOURCES, BS_ON_ENTRY, lock, NULL);
#endif
