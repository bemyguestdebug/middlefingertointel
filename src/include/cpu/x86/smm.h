/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SMM (System Management Mode) helper definitions for x86
 * Reworked for clarity and safety — preserves original semantics.
 */

#pragma once

#include <arch/cpu.h>
#include <commonlib/region.h>
#include <device/pci_type.h>
#include <device/resource.h>
#include <types.h> /* provides u8, u16, u32, u64, uintptr_t, etc. */

/* Default SMM layout used by C code. Keep in sync with linker/stub. */
#define SMM_DEFAULT_BASE    0x30000U
#define SMM_DEFAULT_SIZE    0x10000U

/* Legacy C-side mapping (used only by C programs). */
#define SMM_BASE            0xA0000U

#define SMM_ENTRY_OFFSET    0x8000U
#define SMM_SAVE_STATE_BEGIN(x)  (SMM_ENTRY_OFFSET + (x))

/* ACPI/SMI port defines (PM1a_CNT / APM_CNT variants used by some platforms) */
#define APM_CNT                     0xB2U
#define APM_CNT_NOOP_SMI            0x00U
#define APM_CNT_ACPI_DISABLE        0x1EU
#define APM_CNT_ACPI_ENABLE         0xE1U
#define APM_CNT_ROUTE_ALL_XHCI      0xCAU
#define APM_CNT_FINALIZE            0xCBU
#define APM_CNT_LEGACY              0xCCU
#define APM_CNT_MBI_UPDATE          0xEBU
#define APM_CNT_SMMINFO             0xECU
#define APM_CNT_SMMSTORE            0xEDU
#define APM_CNT_ELOG_GSMI           0xEFU
#define APM_STS                     0xB3U

#define SMM_PCI_RESOURCE_STORE_NUM_RESOURCES 6U

/* STM PSD size is conditional on CONFIG(STM) — keep as compile-time constant. */
#if CONFIG(STM)
# define STM_PSD_SIZE  ALIGN_UP(sizeof(TXT_PROCESSOR_SMM_DESCRIPTOR), 0x100)
#else
# define STM_PSD_SIZE  0
#endif

/* APM control helpers */
enum cb_err apm_control(u8 cmd);
u8 apm_get_apmc(void);

/* SMI handlers (platform hooks) */
void io_trap_handler(int smif);
int mainboard_io_trap_handler(int smif);

void southbridge_smi_set_eos(void);

void global_smi_enable(void);
void global_smi_enable_no_pwrbtn(void);

void cpu_smi_handler(void);
void northbridge_smi_handler(void);
void southbridge_smi_handler(void);

void mainboard_smi_gpi(u32 gpi_sts);
int  mainboard_smi_apmc(u8 data);
void mainboard_smi_sleep(u8 slp_typ);
void mainboard_smi_finalize(void);
int  mainboard_set_smm_log_level(void);

void smm_soc_early_init(void);
void smm_soc_exit(void);

/* SMM handler binary symbols (linked in by the build). */
extern unsigned char _binary_smm_start[];
extern unsigned char _binary_smm_end[];

/* Stores basic PCI device info + BARs/resources (for persisted store). */
struct smm_pci_resource_info {
	pci_devfn_t pci_addr;
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t class_device;
	uint8_t  class_prog;
	struct resource resources[SMM_PCI_RESOURCE_STORE_NUM_RESOURCES];
} __packed;

/* Runtime state for SMM subsystem. Kept small and explicit. */
struct smm_runtime {
	u32     smbase;
	u32     smm_size;
	u32     save_state_size;
	u32     num_cpus;
	u32     gnvs_ptr;
	u32     cbmemc_size;
	void   *cbmemc;
#if CONFIG(SMM_PCI_RESOURCE_STORE)
	struct smm_pci_resource_info pci_resources[CONFIG_SMM_PCI_RESOURCE_STORE_NUM_SLOTS];
#endif
	uintptr_t save_state_top[CONFIG_MAX_CPUS];
	int      smm_log_level;
	uintptr_t smmstore_com_buffer_base;
	size_t   smmstore_com_buffer_size;
} __packed;

/* Parameters provided to SMM module code (stack canary pointer etc). */
struct smm_module_params {
	size_t cpu;
	/* pointer to a canary value at end of stack; used by stub to detect
	 * stack overflows. */
	const uintptr_t *canary;
} __packed;

/* Parameters passed to SMM stub loader and handlers. */
struct smm_stub_params {
	u32 stack_size;
	u32 stack_top;
	u32 c_handler;
	u32 cr3;
	/* apic_id_to_cpu: maps APIC id -> logical CPU index. */
	u16 apic_id_to_cpu[CONFIG_MAX_CPUS];
} __packed;

/* Type of SMM handler entrypoint (asmlinkage conventions). */
typedef asmlinkage void (*smm_handler_t)(void *params);

/* Global NVS pointer (available only in SMM environment). */
#if ENV_SMM
extern struct global_nvs *gnvs;
#endif

/* Entry point used by SMM modules. */
asmlinkage void smm_handler_start(void *params);

/* Retrieve save state area for a given CPU. Caller must ensure CPU has save state. */
void *smm_get_save_state(int cpu);

/* Check overlaps between a region and SMM reserved area. */
bool smm_region_overlaps_handler(const struct region *r);

/* Return true if pointer+len might point into SMRAM. Play safe on failure. */
static inline bool smm_points_to_smram(const void *ptr, size_t len)
{
	struct region r;

	if (region_create_untrusted(&r, (uintptr_t)ptr, len) != CB_SUCCESS)
		return true; /* conservative: assume overlap on error */

	return smm_region_overlaps_handler(&r);
}

/* SMM loader parameters used when allocating stacks/save-state. */
struct smm_loader_params {
	size_t num_cpus;
	size_t cpu_save_state_size;
	size_t num_concurrent_save_states;
	smm_handler_t handler; /* optional */
	uint32_t cr3;
};

/* Return codes: 0 on success, negative on failure. */
int smm_setup_stack(const uintptr_t perm_smbase, const size_t perm_smram_size,
		    const unsigned int total_cpus, const size_t stack_size);
int smm_setup_relocation_handler(struct smm_loader_params *params);
int smm_load_module(uintptr_t smram_base, size_t smram_size, struct smm_loader_params *params);

u32 smm_get_cpu_smbase(unsigned int cpu_num);

/* Backup / restore default SMM area helpers. */
void *backup_default_smm_area(void);
void restore_default_smm_area(void *smm_save_area);

/* Fill arguments for chipset-protected SMM region (e.g., TSEG). */
void smm_region(uintptr_t *start, size_t *size);

/* Provide a default SMM code/stack area for legacy users. */
static inline void aseg_region(uintptr_t *start, size_t *size)
{
	*start = SMM_BASE;
	*size  = SMM_DEFAULT_SIZE;
}

/* SMM subregion enum */
enum {
	SMM_SUBREGION_HANDLER,
	SMM_SUBREGION_CACHE,
	SMM_SUBREGION_CHIPSET,
	SMM_SUBREGION_NUM,
};

/* Get start/size of a specific subregion. Returns 0 on success, negative on error. */
int smm_subregion(int sub, uintptr_t *start, size_t *size);

/* Print SMM memory layout. */
void smm_list_regions(void);

/* SMM save-state revision fetch (from top of save-state area). */
#define SMM_REVISION_OFFSET_FROM_TOP  (0x8000U - 0x7efcU)
uint32_t smm_revision(void);

/* PM ACPI SMI port (commonly APM_CNT on Intel). */
uint16_t pm_acpi_smi_cmd_port(void);

/* PCI resource-store accessors. These return pointers into the persistent store. */
const volatile struct smm_pci_resource_info *smm_get_pci_resource_store(void);

void smm_pci_get_stored_resources(const volatile struct smm_pci_resource_info **out_slots,
				  size_t *out_size);

/* Weak handler to allow platform to initialize PCI resource store. */
void smm_mainboard_pci_resource_store_init(struct smm_pci_resource_info *slots, size_t size);

/* Helper to fill BARs/resources from an array of devices. */
bool smm_pci_resource_store_fill_resources(struct smm_pci_resource_info *slots,
					   size_t num_slots,
					   const struct device **devices,
					   size_t num_devices);

/* Initialize SMM PCI resource store from runtime. */
void smm_pci_resource_store_init(struct smm_runtime *smm_runtime);

/* Retrieve SMMSTORE communication buffer bounds. */
void smm_get_smmstore_com_buffer(uintptr_t *base, size_t *size);
