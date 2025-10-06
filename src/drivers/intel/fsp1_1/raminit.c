/* SPDX-License-Identifier: GPL-2.0-only */

#include <acpi/acpi.h>
#include <cbmem.h>
#include <cf9_reset.h>
#include <commonlib/helpers.h>
#include <console/console.h>
#include <cpu/x86/smm.h>
#include <fsp/romstage.h>
#include <fsp/util.h>
#include <string.h>
#include <timestamp.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Initialize DRAM via FSP, migrate CBMEM, and validate FSP HOBs.
 */
void raminit(struct romstage_params *params)
{
    bool s3wake;
    const EFI_GUID bootldr_tolum_guid = FSP_BOOTLOADER_TOLUM_HOB_GUID;
    const EFI_GUID fsp_reserved_guid = FSP_RESERVED_MEMORY_RESOURCE_HOB_GUID;
    const EFI_GUID memory_info_hob_guid = FSP_SMBIOS_MEMORY_INFO_GUID;
    const EFI_GUID mrc_guid = FSP_NON_VOLATILE_STORAGE_HOB_GUID;

    EFI_HOB_RESOURCE_DESCRIPTOR *cbmem_root = NULL;
    EFI_HOB_RESOURCE_DESCRIPTOR *fsp_memory = NULL;
    FSP_INFO_HEADER *fsp_header = NULL;
    FSP_MEMORY_INIT fsp_memory_init = NULL;
    FSP_MEMORY_INIT_PARAMS fsp_memory_init_params = {0};
    FSP_INIT_RT_COMMON_BUFFER fsp_rt_common_buffer = {0};
    void *hob_list_ptr = NULL;
    FSP_SMBIOS_MEMORY_INFO *memory_info_hob = NULL;
    u32 *mrc_hob = NULL;
    uintptr_t smm_base = 0;
    size_t smm_size = 0;
    u32 fsp_reserved_bytes = 0;
    MEMORY_INIT_UPD memory_init_params = {0};
    MEMORY_INIT_UPD *original_params = NULL;
    EFI_STATUS status = 0;
    VPD_DATA_REGION *vpd_ptr = NULL;
    UPD_DATA_REGION *upd_ptr = NULL;
    int fsp_verification_failure = 0;
    EFI_PEI_HOB_POINTERS hob_ptr = { .Raw = NULL };

    if (params == NULL) {
        die_with_post_code(POSTCODE_RAM_FAILURE,
            "raminit: params pointer is NULL!\n");
    }

    s3wake = (params->power_state != NULL) &&
            (params->power_state->prev_sleep_state == ACPI_S3);

    fsp_header = params->chipset_context;
    if (fsp_header == NULL) {
        die_with_post_code(POSTCODE_RAM_FAILURE,
            "raminit: FSP header is NULL!\n");
    }

    /* Derive VPD and UPD pointers. Validate offsets. */
    {
        uintptr_t base = (uintptr_t)fsp_header->ImageBase;
        uintptr_t vpd_off = fsp_header->CfgRegionOffset;
        if (vpd_off == 0) {
            die_with_post_code(POSTCODE_INVALID_VENDOR_BINARY,
                "raminit: CfgRegionOffset is zero!\n");
        }
        vpd_ptr = (VPD_DATA_REGION *)(base + vpd_off);
        printk(BIOS_DEBUG, "VPD Data at %p\n", vpd_ptr);

        uintptr_t upd_off = vpd_ptr->PcdUpdRegionOffset;
        if (upd_off == 0) {
            die_with_post_code(POSTCODE_INVALID_VENDOR_BINARY,
                "raminit: PcdUpdRegionOffset is zero!\n");
        }
        upd_ptr = (UPD_DATA_REGION *)(base + upd_off);
        printk(BIOS_DEBUG, "UPD Data at %p\n", upd_ptr);

        /* original_params must lie within bounds of UPD region */
        uintptr_t orig_off = (uintptr_t)upd_ptr + upd_ptr->MemoryInitUpdOffset;
        original_params = (MEMORY_INIT_UPD *)orig_off;
    }

    /* Copy UPD-based memory init params */
    memcpy(&memory_init_params, original_params, sizeof(memory_init_params));

    /* Zero RT buffer before assignment */
    memset(&fsp_rt_common_buffer, 0, sizeof(fsp_rt_common_buffer));

    if (s3wake) {
        fsp_rt_common_buffer.BootMode = BOOT_ON_S3_RESUME;
    } else if (params->saved_data != NULL) {
        fsp_rt_common_buffer.BootMode = BOOT_ASSUMING_NO_CONFIGURATION_CHANGES;
    } else {
        fsp_rt_common_buffer.BootMode = BOOT_WITH_FULL_CONFIGURATION;
    }
    fsp_rt_common_buffer.UpdDataRgnPtr = &memory_init_params;
    fsp_rt_common_buffer.BootLoaderTolumSize = cbmem_overhead_size();

    /* Populate FSP memory init parameter structure */
    fsp_memory_init_params.NvsBufferPtr = params->saved_data;
    fsp_memory_init_params.RtBufferPtr = &fsp_rt_common_buffer;
    fsp_memory_init_params.HobListPtr = &hob_list_ptr;

    /* Allow board/SoC to adjust memory_init_params */
    soc_memory_init_params(params, &memory_init_params);
    mainboard_memory_init_params(params, &memory_init_params);
    if (CONFIG(MMA)) {
        setup_mma(&memory_init_params);
    }

    if (CONFIG(DISPLAY_UPD_DATA)) {
        soc_display_memory_init_params(original_params, &memory_init_params);
    }

    /* Invoke FspMemoryInit via function pointer */
    fsp_memory_init = (FSP_MEMORY_INIT)(
        (uintptr_t)fsp_header->ImageBase +
        fsp_header->FspMemoryInitEntryOffset
    );
    printk(BIOS_DEBUG, "Calling FspMemoryInit at %p\n", fsp_memory_init);
    printk(BIOS_SPEW, "    NvsBufferPtr = %p\n",
        fsp_memory_init_params.NvsBufferPtr);
    printk(BIOS_SPEW, "    RtBufferPtr = %p\n",
        fsp_memory_init_params.RtBufferPtr);
    printk(BIOS_SPEW, "    HobListPtr = %p\n",
        fsp_memory_init_params.HobListPtr);

    timestamp_add_now(TS_FSP_MEMORY_INIT_START);
    post_code(POSTCODE_FSP_MEMORY_INIT);

    status = fsp_memory_init(&fsp_memory_init_params);

    mainboard_after_memory_init();
    post_code(0x37);
    timestamp_add_now(TS_FSP_MEMORY_INIT_END);

    printk(BIOS_DEBUG, "FspMemoryInit returned 0x%08x\n", status);
    if (status != EFI_SUCCESS) {
        die_with_post_code(POSTCODE_RAM_FAILURE,
            "ERROR - FspMemoryInit failed!\n");
    }

    /* Locate FSP reserved memory HOB */
    fsp_memory = get_resource_hob(&fsp_reserved_guid, hob_list_ptr);
    if (fsp_memory == NULL) {
        fsp_verification_failure = 1;
        printk(BIOS_ERR,
            "FSP reserved memory HOB missing!\n");
    } else {
        fsp_reserved_bytes = fsp_memory->ResourceLength;
        printk(BIOS_DEBUG, "Reserved 0x%016lx bytes for FSP\n",
            (unsigned long)fsp_reserved_bytes);
    }

    /* If SMI handler is present, log SMM region */
    if (CONFIG(HAVE_SMI_HANDLER)) {
        smm_region(&smm_base, &smm_size);
        printk(BIOS_DEBUG, "smm_size = 0x%08x, smm_base = 0x%08x\n",
            (unsigned int)smm_size, (unsigned int)smm_base);
    }

    printk(BIOS_DEBUG, "cbmem_top = %lx\n", cbmem_top());

    if (!s3wake) {
        cbmem_initialize_empty_id_size(CBMEM_ID_FSP_RESERVED_MEMORY,
            fsp_reserved_bytes);
    } else {
        if (cbmem_initialize_id_size(CBMEM_ID_FSP_RESERVED_MEMORY,
                fsp_reserved_bytes) != 0) {
            printk(BIOS_DEBUG,
                "Failed to recover CBMEM on S3 resume.\n");
            full_reset();
        }
    }

    /* Save runtime configuration for FSP */
    fsp_set_runtime(fsp_header, hob_list_ptr);

    /* Locate CBMEM root (Bootloader Tolum) HOB */
    cbmem_root = get_resource_hob(&bootldr_tolum_guid, hob_list_ptr);
    if (cbmem_root == NULL) {
        fsp_verification_failure = 1;
        printk(BIOS_ERR, "Bootloader Tolum HOB missing!\n");
        printk(BIOS_ERR, "BootLoaderTolumSize = 0x%08x\n",
            fsp_rt_common_buffer.BootLoaderTolumSize);
    }

    /* Locate SMBIOS memory info HOB */
    memory_info_hob = get_guid_hob(&memory_info_hob_guid, hob_list_ptr);
    if (memory_info_hob == NULL) {
        printk(BIOS_ERR, "SMBIOS memory info HOB missing!\n");
        fsp_verification_failure = 1;
    }

    if (hob_list_ptr == NULL) {
        die_with_post_code(POSTCODE_RAM_FAILURE,
            "ERROR - HOB list pointer is NULL!\n");
    }

    hob_ptr.Raw = get_guid_hob(&mrc_guid, hob_list_ptr);
    if ((hob_ptr.Raw == NULL) && (params->saved_data == NULL)) {
        printk(BIOS_ERR, "Non-volatile storage HOB missing!\n");
        fsp_verification_failure = 1;
    }

    if (fsp_verification_failure) {
        printk(BIOS_ERR, "Missing required FSP HOB(s)!\n");
    }

    if (CONFIG(DISPLAY_HOBS)) {
        print_hob_type_structure(0, hob_list_ptr);
    }

    void *fsp_reserved_memory_area = cbmem_find(CBMEM_ID_FSP_RESERVED_MEMORY);
    printk(BIOS_DEBUG, "fsp_reserved_memory_area = %p\n",
        fsp_reserved_memory_area);

    if (fsp_memory != NULL && cbmem_root != NULL) {
        if (cbmem_root->PhysicalStart <= fsp_memory->PhysicalStart) {
            fsp_verification_failure = 1;
            printk(BIOS_ERR,
                "FSP reserved memory above CBMEM root!\n");
        }
    }

    if (fsp_memory != NULL) {
        if (fsp_reserved_memory_area == NULL ||
            fsp_memory->PhysicalStart != (unsigned long)fsp_reserved_memory_area) {
            fsp_verification_failure = 1;
            printk(BIOS_ERR, "Mismatch in FSP reserved memory area!\n");

            if (CONFIG(HAVE_SMI_HANDLER) && cbmem_root != NULL) {
                size_t delta_bytes = smm_base
                    - cbmem_root->PhysicalStart
                    - cbmem_root->ResourceLength;
                printk(BIOS_ERR,
                    "Chipset reserved bytes: 0x%08x\n",
                    (unsigned int)delta_bytes);
                die_with_post_code(POSTCODE_INVALID_VENDOR_BINARY,
                    "Invalid chipset reserved region size!\n");
            }
        }
    }

    if (fsp_verification_failure) {
        die_with_post_code(POSTCODE_INVALID_VENDOR_BINARY,
            "ERROR - coreboot requirements not met by FSP binary!\n");
    }

    /* Capture MRC data for next boot */
    mrc_hob = get_guid_hob(&mrc_guid, hob_list_ptr);
    if (mrc_hob != NULL) {
        params->data_to_save = GET_GUID_HOB_DATA(mrc_hob);
        params->data_to_save_size = ALIGN_UP(
            (u32)GET_HOB_LENGTH(mrc_hob), 16);
    } else {
        printk(BIOS_DEBUG,
            "Memory configuration HOB not present\n");
        params->data_to_save = NULL;
        params->data_to_save_size = 0;
    }
}

/* Weak fallback for mainboard_after_memory_init */
__weak void mainboard_after_memory_init(void)
{
    printk(BIOS_DEBUG, "WEAK: %s/%s called\n", __FILE__, __func__);
}
