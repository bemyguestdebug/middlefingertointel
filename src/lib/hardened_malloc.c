#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <console/console.h>
#include <cbmem.h>
//local
#include "hardened_malloc.h"


static inline uint32_t read_canary(void *ptr) {
    return *(uint32_t *)ptr;
}

static inline void write_canary(void *ptr) {
    *(uint32_t *)ptr = CANARY_VALUE;
}

static void check_canary(void *ptr) {
    if (read_canary(ptr) != CANARY_VALUE) {
        printk(BIOS_ERR, "hardened_malloc: buffer overflow detected!\n");
        // Optionally: halt or raise a panic here
    }
}

void *hardened_malloc(size_t size) {
    // Add extra space for the canary
    size_t total_size = size + CANARY_SIZE;

    void *real_ptr = malloc(total_size);
    if (!real_ptr)
        return NULL;

    // Write the canary at the end
    void *canary_ptr = (uint8_t *)real_ptr + size;
    write_canary(canary_ptr);

    // Track this allocation
    for (int i = 0; i < MAX_HARDENED_ALLOCATIONS; i++) {
        if (alloc_table[i].user_ptr == NULL) {
            alloc_table[i].user_ptr = real_ptr;
            alloc_table[i].size = size;
            break;
        }
    }

    return real_ptr;
}

void hardened_free(void *ptr) {
    if (!ptr)
        return;

    for (int i = 0; i < MAX_HARDENED_ALLOCATIONS; i++) {
        if (alloc_table[i].user_ptr == ptr) {
            void *canary_ptr = (uint8_t *)ptr + alloc_table[i].size;
            check_canary(canary_ptr);

            // Free and clear
            free(ptr);
            alloc_table[i].user_ptr = NULL;
            alloc_table[i].size = 0;
            return;
        }
    }

    printk(BIOS_ERR, "hardened_free: unknown pointer!\n");
}


//done
