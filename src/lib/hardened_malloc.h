
#ifndef HARDENED_MALLOC_H
#define HARDENED_MALLOC_H


#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <console/console.h>
#include <cbmem.h>

#define MAX_HARDENED_ALLOCATIONS 128
#define CANARY_VALUE 0xBADC0DE
#define CANARY_SIZE sizeof(uint32_t)

typedef struct {
    void *user_ptr;
    size_t size;
} alloc_entry_t;

static alloc_entry_t alloc_table[MAX_HARDENED_ALLOCATIONS];

static void check_canary(void *ptr);
void hardened_free(void *ptr);
void *hardened_malloc(size_t size);
static inline void write_canary(void *ptr);
static inline uint32_t read_canary(void *ptr);

#endif // HARDENED_MALLOC_H
