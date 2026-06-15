#include "osal_psram.h"
#include "app_graphics_mem.h"
#include "app_graphics_ospi.h"
#include "osal_log.h"
#include "osal.h"
#include <string.h>
#include "platform_sdk.h"

static bool s_psram_initialized = false;

int32_t osal_psram_init(void)
{
    if (s_psram_initialized)
    {
        return OSAL_SUCCESS;
    }

    app_graphics_ospi_params_t params = PSRAM_INIT_PARAMS_Default;
    int32_t ret = app_graphics_ospi_init(&params);
    if (ret != 0)
    {
        OSAL_LOG_ERR("PSRAM OSPI init failed: %d", ret);
        return OSAL_ERROR;
    }

    mem_pwr_mgmt_mode_set(MEM_POWER_FULL_MODE);
    app_graphics_mem_init((void*)GFX_MEM_BASE, GFX_MEM_SIZE);

    s_psram_initialized = true;
    OSAL_LOG_INF("PSRAM initialized, base: 0x%08X, size: %lu", GFX_MEM_BASE, GFX_MEM_SIZE);
    return OSAL_SUCCESS;
}

void* osal_psram_malloc(uint32_t size)
{
    if (!s_psram_initialized)
    {
        OSAL_LOG_ERR("PSRAM not initialized");
        return NULL;
    }

    if (size == 0)
    {
        return NULL;
    }

    void *ptr = app_graphics_mem_malloc(size);
    if (ptr == NULL)
    {
        OSAL_LOG_ERR("PSRAM malloc failed, size: %u", size);
    }
    return ptr;
}

void osal_psram_free(void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }

    if (!s_psram_initialized)
    {
        OSAL_LOG_ERR("PSRAM not initialized");
        return;
    }

    app_graphics_mem_free(ptr);
}

uint32_t osal_psram_max_alloc_addr_get(void)
{
    if (!s_psram_initialized)
    {
        return 0;
    }
    return app_graphics_mem_max_alloc_addr_get();
}
