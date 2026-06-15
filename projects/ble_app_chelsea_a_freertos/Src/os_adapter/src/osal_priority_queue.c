#include "osal_priority_queue.h"
#include "osal_heap.h"
#include "osal_mutex.h"
#include "osal_sema.h"
#include "osal_log.h"
#include "osal.h"
#include <string.h>

typedef struct {
    osal_event_t *high_head;
    osal_event_t *high_tail;
    osal_event_t *low_head;
    osal_event_t *low_tail;
    osal_mutex_handle_t mutex;
    osal_sema_handle_t sema;
    uint32_t high_count;
    uint32_t low_count;
} osal_priority_queue_t;

int32_t osal_priority_queue_create(osal_priority_queue_handle_t *handle)
{
    if (handle == NULL)
    {
        return OSAL_INVALID_POINTER;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)osal_heap_malloc(sizeof(osal_priority_queue_t));
    if (queue == NULL)
    {
        OSAL_LOG_ERR("Failed to allocate priority queue");
        return OSAL_ERROR;
    }

    memset(queue, 0, sizeof(osal_priority_queue_t));

    int32_t ret = osal_mutex_create(&queue->mutex);
    if (ret != OSAL_SUCCESS)
    {
        OSAL_LOG_ERR("Failed to create mutex");
        osal_heap_free(queue);
        return ret;
    }

    ret = osal_sema_countings_create(&queue->sema, 0xFFFFFFFF, 0);
    if (ret != OSAL_SUCCESS)
    {
        OSAL_LOG_ERR("Failed to create semaphore");
        osal_mutex_delete(queue->mutex);
        osal_heap_free(queue);
        return ret;
    }

    *handle = (osal_priority_queue_handle_t)queue;
    return OSAL_SUCCESS;
}

void osal_priority_queue_delete(osal_priority_queue_handle_t handle)
{
    if (handle == NULL)
    {
        return;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;

    osal_event_t *event = queue->high_head;
    while (event != NULL)
    {
        osal_event_t *next = event->next;
        osal_event_destroy(event);
        event = next;
    }

    event = queue->low_head;
    while (event != NULL)
    {
        osal_event_t *next = event->next;
        osal_event_destroy(event);
        event = next;
    }

    osal_mutex_delete(queue->mutex);
    osal_sema_delete(queue->sema);
    osal_heap_free(queue);
}

int32_t osal_priority_queue_send(osal_priority_queue_handle_t handle, osal_event_t *event)
{
    if (handle == NULL || event == NULL)
    {
        return OSAL_INVALID_POINTER;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;

    int32_t ret = osal_mutex_take(queue->mutex, OSAL_MAX_DELAY);
    if (ret != OSAL_SUCCESS)
    {
        OSAL_LOG_ERR("Failed to take mutex");
        return ret;
    }

    event->next = NULL;

    if (event->priority == OSAL_EVENT_PRIORITY_HIGH)
    {
        if (queue->high_tail == NULL)
        {
            queue->high_head = event;
            queue->high_tail = event;
        }
        else
        {
            queue->high_tail->next = event;
            queue->high_tail = event;
        }
        queue->high_count++;
    }
    else
    {
        if (queue->low_tail == NULL)
        {
            queue->low_head = event;
            queue->low_tail = event;
        }
        else
        {
            queue->low_tail->next = event;
            queue->low_tail = event;
        }
        queue->low_count++;
    }

    osal_mutex_give(queue->mutex);
    osal_sema_give(queue->sema);

    return OSAL_SUCCESS;
}

int32_t osal_priority_queue_send_front(osal_priority_queue_handle_t handle, osal_event_t *event)
{
    if (handle == NULL || event == NULL)
    {
        return OSAL_INVALID_POINTER;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;

    int32_t ret = osal_mutex_take(queue->mutex, OSAL_MAX_DELAY);
    if (ret != OSAL_SUCCESS)
    {
        OSAL_LOG_ERR("Failed to take mutex");
        return ret;
    }

    event->next = NULL;

    if (event->priority == OSAL_EVENT_PRIORITY_HIGH)
    {
        if (queue->high_head == NULL)
        {
            queue->high_head = event;
            queue->high_tail = event;
        }
        else
        {
            event->next = queue->high_head;
            queue->high_head = event;
        }
        queue->high_count++;
    }
    else
    {
        if (queue->low_head == NULL)
        {
            queue->low_head = event;
            queue->low_tail = event;
        }
        else
        {
            event->next = queue->low_head;
            queue->low_head = event;
        }
        queue->low_count++;
    }

    osal_mutex_give(queue->mutex);
    osal_sema_give(queue->sema);

    return OSAL_SUCCESS;
}

int32_t osal_priority_queue_receive(osal_priority_queue_handle_t handle, osal_event_t **event, osal_tick_type_t timeout)
{
    if (handle == NULL || event == NULL)
    {
        return OSAL_INVALID_POINTER;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;

    int32_t ret = osal_sema_take(queue->sema, timeout);
    if (ret != OSAL_SUCCESS)
    {
        return OSAL_ERROR_TIMEOUT;
    }

    ret = osal_mutex_take(queue->mutex, OSAL_MAX_DELAY);
    if (ret != OSAL_SUCCESS)
    {
        OSAL_LOG_ERR("Failed to take mutex");
        return ret;
    }

    osal_event_t *result = NULL;

    if (queue->high_head != NULL)
    {
        result = queue->high_head;
        queue->high_head = result->next;
        if (queue->high_head == NULL)
        {
            queue->high_tail = NULL;
        }
        queue->high_count--;
    }
    else if (queue->low_head != NULL)
    {
        result = queue->low_head;
        queue->low_head = result->next;
        if (queue->low_head == NULL)
        {
            queue->low_tail = NULL;
        }
        queue->low_count--;
    }

    if (result != NULL)
    {
        result->next = NULL;
    }

    osal_mutex_give(queue->mutex);

    *event = result;
    return OSAL_SUCCESS;
}

uint32_t osal_priority_queue_high_count(osal_priority_queue_handle_t handle)
{
    if (handle == NULL)
    {
        return 0;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;
    return queue->high_count;
}

uint32_t osal_priority_queue_low_count(osal_priority_queue_handle_t handle)
{
    if (handle == NULL)
    {
        return 0;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;
    return queue->low_count;
}

uint32_t osal_priority_queue_total_count(osal_priority_queue_handle_t handle)
{
    if (handle == NULL)
    {
        return 0;
    }

    osal_priority_queue_t *queue = (osal_priority_queue_t*)handle;
    return queue->high_count + queue->low_count;
}
