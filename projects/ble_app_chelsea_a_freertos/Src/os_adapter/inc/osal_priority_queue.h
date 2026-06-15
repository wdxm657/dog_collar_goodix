#ifndef __OSAL_PRIORITY_QUEUE_H__
#define __OSAL_PRIORITY_QUEUE_H__

#include "common_types.h"
#include "osal_event.h"

typedef void* osal_priority_queue_handle_t;

int32_t osal_priority_queue_create(osal_priority_queue_handle_t *handle);

void osal_priority_queue_delete(osal_priority_queue_handle_t handle);

int32_t osal_priority_queue_send(osal_priority_queue_handle_t handle, osal_event_t *event);

int32_t osal_priority_queue_send_front(osal_priority_queue_handle_t handle, osal_event_t *event);

int32_t osal_priority_queue_receive(osal_priority_queue_handle_t handle, osal_event_t **event, osal_tick_type_t timeout);

uint32_t osal_priority_queue_high_count(osal_priority_queue_handle_t handle);

uint32_t osal_priority_queue_low_count(osal_priority_queue_handle_t handle);

uint32_t osal_priority_queue_total_count(osal_priority_queue_handle_t handle);

#endif
