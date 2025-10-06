#ifndef MALLOC_OVERRIDE_H
#define MALLOC_OVERRIDE_H

//testing: test_code_hardenedmalloc_aa1aa0d2a3eebd8
#include "hardened_malloc.h"

#define malloc(size) hardened_malloc(size)
#define free(ptr)    hardened_free(ptr)

#endif
