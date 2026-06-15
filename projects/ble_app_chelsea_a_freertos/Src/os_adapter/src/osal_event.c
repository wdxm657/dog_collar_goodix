#include "osal_event.h"
#include "osal_psram.h"
#include "osal_heap.h"
#include "osal_log.h"
#include "osal.h"
#include <string.h>

osal_event_t* osal_event_create(uint8_t type, uint8_t priority, const uint8_t *data, uint32_t size)
{
    if (data == NULL && size > 0)
    {
        OSAL_LOG_ERR("Invalid event data pointer");
        return NULL;
    }

    if (priority != OSAL_EVENT_PRIORITY_LOW && priority != OSAL_EVENT_PRIORITY_HIGH)
    {
        OSAL_LOG_ERR("Invalid priority: %d", priority);
        return NULL;
    }

    osal_event_t *event = (osal_event_t*)osal_heap_malloc(sizeof(osal_event_t));
    if (event == NULL)
    {
        OSAL_LOG_ERR("Failed to allocate event structure");
        return NULL;
    }

    memset(event, 0, sizeof(osal_event_t));
    event->event_type = type;
    event->priority = priority;
    event->data_size = size;
    event->retry_count = 0;
    event->next = NULL;

    if (size > 0)
    {
        event->data = (uint8_t*)osal_psram_malloc(size);
        if (event->data == NULL)
        {
            OSAL_LOG_ERR("Failed to allocate event data in PSRAM, size: %u", size);
            osal_heap_free(event);
            return NULL;
        }
        memcpy(event->data, data, size);
    }
    else
    {
        event->data = NULL;
    }

    return event;
}

void osal_event_destroy(osal_event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    if (event->data != NULL)
    {
        osal_psram_free(event->data);
        event->data = NULL;
    }

    osal_heap_free(event);
}

osal_event_t* osal_event_clone(osal_event_t *event)
{
    if (event == NULL)
    {
        return NULL;
    }

    return osal_event_create(event->event_type, event->priority, event->data, event->data_size);
}
