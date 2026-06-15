#ifndef __OSAL_EVENT_H__
#define __OSAL_EVENT_H__

#include <stdint.h>
#include "common_types.h"

#define OSAL_EVENT_PRIORITY_LOW     (1)
#define OSAL_EVENT_PRIORITY_HIGH    (2)

#define OSAL_EVENT_MAX_RETRY_COUNT  (3)

typedef struct osal_event_t osal_event_t;

struct osal_event_t {
    uint8_t event_type;
    uint8_t priority;
    uint8_t *data;
    uint32_t data_size;
    uint8_t retry_count;
    osal_event_t *next;
};

osal_event_t* osal_event_create(uint8_t type, uint8_t priority, const uint8_t *data, uint32_t size);

void osal_event_destroy(osal_event_t *event);

osal_event_t* osal_event_clone(osal_event_t *event);

#endif
