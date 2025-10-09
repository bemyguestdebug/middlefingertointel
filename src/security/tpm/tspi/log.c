/* SPDX-License-Identifier: GPL-2.0-only */

#include <console/console.h>
#include <security/tpm/tspi/logs.h>
#include <security/tpm/tspi.h>
#include <region_file.h>
#include <string.h>
#include <symbols.h>
#include <cbmem.h>
#include <vb2_sha.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

/* Helper: portable, small secure zero to avoid compiler optimizing out clears.
 * Uses volatile pointer write to ensure stores happen. */
static void secure_zero(void *v, size_t n)
{
	if (!v || n == 0)
		return;
	volatile unsigned char *p = (volatile unsigned char *)v;
	while (n--) {
		*p++ = 0;
	}
}

/* Helper: always NUL-terminate, copy at most (dst_size - 1) bytes.
 * Safe replacement for strncpy which may not NUL-terminate. */
static void safe_strncpy(char *dst, const char *src, size_t dst_size)
{
	if (!dst || dst_size == 0)
		return;

	if (!src) {
		dst[0] = '\0';
		return;
	}

	size_t i;
	for (i = 0; i < dst_size - 1 && src[i] != '\0'; ++i)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* Returns pointer to TPM CB log table in cbmem (or NULL). Thread-safety
 * note: original logic used a static cached pointer; keep that but validate
 * the structure on reuse. */
void *tpm_cb_log_cbmem_init(void)
{
	static struct tpm_cb_log_table *tclt = NULL;

	/* If previously cached, sanity-check it before returning. */
	if (tclt) {
		/* Basic sanity: num_entries must not exceed max_entries and not be unreasonably large. */
		if (tclt->max_entries > 0 &&
		    tclt->num_entries <= tclt->max_entries &&
		    tclt->num_entries < (INT_MAX / 2)) {
			return tclt;
		}
		/* If the stored table looks corrupted, clear cache and re-init. */
		tclt = NULL;
	}

	if (!ENV_HAS_CBMEM)
		return NULL;

	/* Try to find existing cbmem record */
	tclt = cbmem_find(CBMEM_ID_TPM_CB_LOG);
	if (!tclt) {
		/* Allocate space large enough for full table */
		size_t tpm_log_len = sizeof(struct tpm_cb_log_table) +
			MAX_TPM_LOG_ENTRIES * sizeof(struct tpm_cb_log_entry);
		/* cbmem_add may fail if not available — handle that case */
		tclt = cbmem_add(CBMEM_ID_TPM_CB_LOG, tpm_log_len);
		if (tclt) {
			/* initialize fields defensively */
			tclt->max_entries = MAX_TPM_LOG_ENTRIES;
			tclt->num_entries = 0;
			/* zero the entries area to avoid uninitialized data */
			if (tpm_log_len > sizeof(struct tpm_cb_log_table))
				secure_zero(&tclt->entries[0],
						MAX_TPM_LOG_ENTRIES * sizeof(struct tpm_cb_log_entry));
		}
	}

	/* final sanity check before returning */
	if (tclt) {
		if (tclt->max_entries == 0 || tclt->num_entries > tclt->max_entries) {
			/* corrupt: treat as not present */
			tclt = NULL;
		}
	}
	return tclt;
}

/* Dump log — defensive checks added so malformed tables don't crash printer. */
void tpm_cb_log_dump(void)
{
	size_t i, j;
	struct tpm_cb_log_table *tclt;

	tclt = tpm_log_init();
	if (!tclt)
		return;

	/* sanity bounds */
	if (tclt->max_entries == 0 || tclt->num_entries > tclt->max_entries) {
		printk(BIOS_WARNING, "TPM LOG: invalid log table metadata\n");
		return;
	}

	printk(BIOS_INFO, "coreboot TPM log measurements:\n\n");
	for (i = 0; i < (size_t)tclt->num_entries; i++) {
		struct tpm_cb_log_entry *tce = &tclt->entries[i];
		if (!tce)
			continue;

		/* clamp digest length to allowed maximum */
		size_t dlen = tce->digest_length;
		if (dlen > TPM_CB_LOG_DIGEST_MAX_LENGTH)
			dlen = TPM_CB_LOG_DIGEST_MAX_LENGTH;

		printk(BIOS_INFO, " PCR-%u ", tce->pcr);

		for (j = 0; j < dlen; j++)
			printk(BIOS_INFO, "%02x", tce->digest[j]);

		/* Ensure strings are NUL-terminated before printing */
		tce->name[TPM_CB_LOG_PCR_HASH_NAME - 1] = '\0';
		tce->digest_type[TPM_CB_LOG_PCR_HASH_LEN - 1] = '\0';

		printk(BIOS_INFO, " %s [%s]\n",
			   tce->digest_type, tce->name);
	}
	printk(BIOS_INFO, "\n");
}

/* Add an entry to the table; defensive parameter checks and zeroing of new entry. */
void tpm_cb_log_add_table_entry(const char *name, const uint32_t pcr,
				enum vb2_hash_algorithm digest_algo,
				const uint8_t *digest,
				const size_t digest_len)
{
	struct tpm_cb_log_table *tclt = tpm_log_init();
	if (!tclt) {
		printk(BIOS_WARNING, "TPM LOG: Log non-existent!\n");
		return;
	}

	/* validate table metadata */
	if (tclt->max_entries == 0 || tclt->num_entries > tclt->max_entries) {
		printk(BIOS_WARNING, "TPM LOG: invalid table metadata\n");
		return;
	}

	if (tclt->num_entries >= tclt->max_entries) {
		printk(BIOS_WARNING, "TPM LOG: log table is full\n");
		return;
	}

	if (!name) {
		printk(BIOS_WARNING, "TPM LOG: entry name not set\n");
		return;
	}

	if (!digest && digest_len != 0) {
		printk(BIOS_WARNING, "TPM LOG: NULL digest pointer\n");
		return;
	}

	if (digest_len > TPM_CB_LOG_DIGEST_MAX_LENGTH) {
		printk(BIOS_WARNING, "TPM LOG: PCR digest too long for log entry\n");
		return;
	}

	/* grab and initialize the next entry */
	struct tpm_cb_log_entry *tce = &tclt->entries[tclt->num_entries];

	/* Zero the whole entry before populating to avoid leaking previous state */
	secure_zero(tce, sizeof(*tce));

	/* safe increment only after success to avoid transient invalid state */
	/* Copy name (safe_strncpy always NUL-terminates) */
	safe_strncpy(tce->name, name, TPM_CB_LOG_PCR_HASH_NAME);

	tce->pcr = pcr;

	/* copy digest type string; guarded against NULL return from helper */
	const char *hash_name = vb2_get_hash_algorithm_name(digest_algo);
	if (!hash_name)
		hash_name = "UNKNOWN";
	safe_strncpy(tce->digest_type, hash_name, TPM_CB_LOG_PCR_HASH_LEN);

	/* copy digest bytes */
	if (digest_len > 0)
		memcpy(tce->digest, digest, digest_len);
	tce->digest_length = (uint8_t)digest_len;

	/* commit the new entry (increment after the entry is fully initialized) */
	tclt->num_entries++;
}

/* Clear pre-ram log: defensive check of pointer and zeroing of used entries. */
void tpm_cb_preram_log_clear(void)
{
	printk(BIOS_INFO, "TPM LOG: clearing preram log\n");
	struct tpm_cb_log_table *tclt = (struct tpm_cb_log_table *)_tpm_log;
	if (!tclt) {
		printk(BIOS_WARNING, "TPM LOG: _tpm_log is NULL\n");
		return;
	}

	/* Zero used entries to avoid leaking prior data */
	if (tclt->num_entries > 0 && tclt->num_entries <= tclt->max_entries) {
		size_t i;
		for (i = 0; i < (size_t)tclt->num_entries && i < tclt->max_entries; ++i)
			secure_zero(&tclt->entries[i], sizeof(struct tpm_cb_log_entry));
	}

	tclt->max_entries = MAX_PRERAM_TPM_LOG_ENTRIES;
	tclt->num_entries = 0;
}

/* Get an entry — returns 0 on success, 1 on error. Returns pointers into internal
 * buffers; caller must not modify them. Defensive checks for user pointers. */
int tpm_cb_log_get(int entry_idx, int *pcr, const uint8_t **digest_data,
		   enum vb2_hash_algorithm *digest_algo, const char **event_name)
{
	struct tpm_cb_log_table *tclt;
	struct tpm_cb_log_entry *tce;
	enum vb2_hash_algorithm algo;

	/* validate caller-provided pointers */
	if (!pcr || !digest_data || !digest_algo || !event_name)
		return 1;

	tclt = tpm_log_init();
	if (!tclt)
		return 1;

	/* validate table metadata */
	if (tclt->max_entries == 0 || tclt->num_entries > tclt->max_entries)
		return 1;

	if (entry_idx < 0 || (size_t)entry_idx >= (size_t)tclt->num_entries)
		return 1;

	tce = &tclt->entries[entry_idx];
	if (!tce)
		return 1;

	*pcr = (int)tce->pcr;
	*digest_data = tce->digest;
	*event_name = tce->name;

	/* map digest type string back to enum safely */
	*digest_algo = VB2_HASH_INVALID;
	for (algo = VB2_HASH_INVALID; algo != VB2_HASH_ALG_COUNT; ++algo) {
		const char *name = vb2_hash_names[algo];
		if (!name)
			continue;
		/* guard against non-terminated digest_type */
		tce->digest_type[TPM_CB_LOG_PCR_HASH_LEN - 1] = '\0';
		if (strcmp(tce->digest_type, name) == 0) {
			*digest_algo = algo;
			break;
		}
	}
	return 0;
}

/* Return number of entries (clamped to uint16_t range) */
uint16_t tpm_cb_log_get_size(const void *log_table)
{
	const struct tpm_cb_log_table *tclt = (const struct tpm_cb_log_table *)log_table;
	if (!tclt)
		return 0;
	/* guard against corruption */
	if (tclt->num_entries > tclt->max_entries)
		return 0;
	if (tclt->num_entries > UINT16_MAX)
		return UINT16_MAX;
	return (uint16_t)tclt->num_entries;
}

/* Copy entries from 'from' to 'to' with bounds checking and defensive copying. */
void tpm_cb_log_copy_entries(const void *from, void *to)
{
	const struct tpm_cb_log_table *from_log = (const struct tpm_cb_log_table *)from;
	struct tpm_cb_log_table *to_log = (struct tpm_cb_log_table *)to;
	size_t i;

	if (!from_log || !to_log)
		return;

	/* validate metadata */
	if (from_log->max_entries == 0 || to_log->max_entries == 0)
		return;

	if (from_log->num_entries > from_log->max_entries)
		return;

	for (i = 0; i < (size_t)from_log->num_entries; i++) {
		/* ensure destination has space */
		if (to_log->num_entries >= to_log->max_entries) {
			printk(BIOS_ERR, "TPM LOG: log table is full\n");
			return;
		}

		struct tpm_cb_log_entry *tce = &to_log->entries[to_log->num_entries];

		/* zero dest entry first */
		secure_zero(tce, sizeof(*tce));

		/* safe copy of name */
		safe_strncpy(tce->name, from_log->entries[i].name, TPM_CB_LOG_PCR_HASH_NAME);

		/* copy PCR */
		tce->pcr = from_log->entries[i].pcr;

		/* clamp digest_length */
		size_t from_dlen = from_log->entries[i].digest_length;
		if (from_dlen > TPM_CB_LOG_DIGEST_MAX_LENGTH) {
			printk(BIOS_WARNING, "TPM LOG: PCR digest too long for log entry\n");
			return;
		}

		/* copy digest type safely */
		safe_strncpy(tce->digest_type,
			     from_log->entries[i].digest_type,
			     TPM_CB_LOG_PCR_HASH_LEN);

		/* copy digest bytes */
		tce->digest_length = (uint8_t)MIN(from_dlen, (size_t)TPM_CB_LOG_DIGEST_MAX_LENGTH);
		if (tce->digest_length > 0)
			memcpy(tce->digest, from_log->entries[i].digest, tce->digest_length);

		/* commit the entry */
		to_log->num_entries++;
	}
}
