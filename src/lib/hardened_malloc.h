#ifndef HARDENED_MALLOC_H
#define HARDENED_MALLOC_H

#include <stddef.h>
#include <stdint.h>

#define MAX_HARDENED_ALLOCATIONS 128
#define CANARY_VALUE 0xBADC0DE
#define CANARY_SIZE sizeof(uint32_t)

typedef struct {
    void *user_ptr;
    size_t size;
} alloc_entry_t;

// Only expose the functions meant to be called outside
void *hardened_malloc(size_t size);
void hardened_free(void *ptr);

#endif // HARDENED_MALLOC_H
