/* SPDX-License-Identifier: GPL-2.0-only */

#include <bootmode.h>
#include <bootsplash.h>
#include <console/console.h>
#include <fsp/ramstage.h>
#include <fsp/util.h>
#include <framebuffer_info.h>
#include <stage_cache.h>
#include <string.h>
#include <timestamp.h>

static void display_hob_info(FSP_INFO_HEADER *fsp_info_header)
{
	const EFI_GUID graphics_info_guid = EFI_PEI_GRAPHICS_INFO_HOB_GUID;
	void *hob_list_ptr = get_hob_list();

	if (!fsp_info_header) {
		printk(BIOS_ERR, "display_hob_info: FSP_INFO_HEADER is NULL\n");
		return;
	}

	if (!hob_list_ptr) {
		printk(BIOS_ERR, "display_hob_info: HOB pointer is NULL\n");
		return;
	}

	if (CONFIG(DISPLAY_HOBS))
		print_hob_type_structure(0, hob_list_ptr);

	if ((fsp_info_header->ImageAttribute & GRAPHICS_SUPPORT_BIT) &&
	    !get_guid_hob(&graphics_info_guid, hob_list_ptr) &&
	    CONFIG(DISPLAY_HOBS)) {
		printk(BIOS_ERR, "7.5: EFI_PEI_GRAPHICS_INFO_HOB missing!\n");
		printk(BIOS_ERR, "Missing one or more required FSP HOBs!\n");
	}
}

static void fsp_run_silicon_init(FSP_INFO_HEADER *fsp_info_header)
{
	FSP_SILICON_INIT fsp_silicon_init;
	SILICON_INIT_UPD silicon_init_params = {0};
	SILICON_INIT_UPD *original_params = NULL;
	UPD_DATA_REGION *upd_ptr = NULL;
	VPD_DATA_REGION *vpd_ptr = NULL;
	EFI_STATUS status;
	uintptr_t base;

	if (!fsp_info_header) {
		printk(BIOS_ERR, "FSP_INFO_HEADER not set!\n");
		return;
	}

	print_fsp_info(fsp_info_header);

	base = (uintptr_t)fsp_info_header->ImageBase;
	if (!base || !fsp_info_header->CfgRegionOffset) {
		printk(BIOS_ERR, "Invalid ImageBase or CfgRegionOffset!\n");
		return;
	}

	/* Resolve VPD and UPD pointer safely */
	vpd_ptr = (VPD_DATA_REGION *)(base + fsp_info_header->CfgRegionOffset);
	printk(BIOS_DEBUG, "%p: VPD Data\n", vpd_ptr);

	if (!vpd_ptr->PcdUpdRegionOffset) {
		printk(BIOS_ERR, "PcdUpdRegionOffset is zero!\n");
		return;
	}
	upd_ptr = (UPD_DATA_REGION *)(base + vpd_ptr->PcdUpdRegionOffset);
	printk(BIOS_DEBUG, "%p: UPD Data\n", upd_ptr);

	if (!upd_ptr->SiliconInitUpdOffset) {
		printk(BIOS_ERR, "SiliconInitUpdOffset is zero!\n");
		return;
	}

	original_params = (SILICON_INIT_UPD *)((u8 *)upd_ptr +
		upd_ptr->SiliconInitUpdOffset);

	memcpy(&silicon_init_params, original_params, sizeof(SILICON_INIT_UPD));

	soc_silicon_init_params(&silicon_init_params);

	if (CONFIG(RUN_FSP_GOP)) {
		load_vbt(&silicon_init_params);
	}

	mainboard_silicon_init_params(&silicon_init_params);

	if (CONFIG(BMP_LOGO)) {
		size_t logo_size = 0;
		silicon_init_params.PcdLogoPtr =
			(uint32_t)(uintptr_t)bmp_load_logo(&logo_size);
		silicon_init_params.PcdLogoSize = (uint32_t)logo_size;
	}

	if (CONFIG(DISPLAY_UPD_DATA)) {
		soc_display_silicon_init_params(original_params,
			&silicon_init_params);
	}

	/* Perform FSP SiliconInit */
	fsp_silicon_init = (FSP_SILICON_INIT)(base +
		fsp_info_header->FspSiliconInitEntryOffset);

	if (!fsp_silicon_init) {
		printk(BIOS_ERR, "Invalid FspSiliconInit function pointer!\n");
		return;
	}

	printk(BIOS_DEBUG, "Calling FspSiliconInit(%p) at %p\n",
		&silicon_init_params, fsp_silicon_init);
	timestamp_add_now(TS_FSP_SILICON_INIT_START);
	post_code(POSTCODE_FSP_SILICON_INIT);

	status = fsp_silicon_init(&silicon_init_params);

	timestamp_add_now(TS_FSP_SILICON_INIT_END);
	printk(BIOS_DEBUG, "FspSiliconInit returned 0x%08x\n", status);

	if (CONFIG(BMP_LOGO))
		bmp_release_logo();

#if CONFIG(RUN_FSP_GOP)
	if (silicon_init_params.GraphicsConfigPtr)
		gfx_set_init_done(1);
#endif

#if CONFIG(RUN_FSP_GOP)
	{
		const EFI_GUID vbt_guid = EFI_PEI_GRAPHICS_INFO_HOB_GUID;
		void *hob_list_ptr = get_hob_list();
		u32 *vbt_hob = get_guid_hob(&vbt_guid, hob_list_ptr);

		if (!vbt_hob) {
			printk(BIOS_ERR, "FSP_ERR: Graphics Data HOB is not present\n");
		} else {
			EFI_PEI_GRAPHICS_INFO_HOB *gop = GET_GUID_HOB_DATA(vbt_hob);

			printk(BIOS_DEBUG, "FSP_DEBUG: Graphics Data HOB present\n");

			fb_add_framebuffer_info(gop->FrameBufferBase,
				gop->GraphicsMode.HorizontalResolution,
				gop->GraphicsMode.VerticalResolution,
				gop->GraphicsMode.PixelsPerScanLine * 4,
				32);
		}
	}
#endif

	display_hob_info(fsp_info_header);
}

static void fsp_load(void)
{
	struct prog fsp = PROG_INIT(PROG_REFCODE, "fsp.bin");

	if (resume_from_stage_cache()) {
		stage_cache_load_stage(STAGE_REFCODE, &fsp);
	} else {
		fsp_relocate(&fsp);

		if (prog_entry(&fsp))
			stage_cache_add(STAGE_REFCODE, &fsp);
	}

	fsp_update_fih(prog_entry(&fsp));
}

void intel_silicon_init(void)
{
	fsp_load();

	FSP_INFO_HEADER *fsp_header = fsp_get_fih();
	if (!fsp_header) {
		printk(BIOS_ERR, "intel_silicon_init: Failed to retrieve FSP header\n");
		return;
	}

	fsp_run_silicon_init(fsp_header);
}

__weak void mainboard_silicon_init_params(SILICON_INIT_UPD *params)
{
	/* Default weak handler; override in mainboard code */
}
