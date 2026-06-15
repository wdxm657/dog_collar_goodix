#ifndef __OSAL_PSRAM_H__
#define __OSAL_PSRAM_H__

#include <stdint.h>
#include "common_types.h"

#define OSAL_PSRAM_MAX_RETRY_COUNT  (3)

int32_t osal_psram_init(void);

void* osal_psram_malloc(uint32_t size);

void osal_psram_free(void *ptr);

uint32_t osal_psram_max_alloc_addr_get(void);

#endif
