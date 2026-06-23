/**
  ****************************************************************************************
  * @file    gh_protocol_user.c
  * @author  GHealth Driver Team
  * @brief   gh protocol user file
  ****************************************************************************************
  * @attention
  #####Copyright (c) 2024 GOODIX
   All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
  * Neither the name of GOODIX nor the names of its contributors may be used
    to endorse or promote products derived from this software without
    specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDERS AND CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

  ****************************************************************************************
  */

/*
 * INCLUDE FILES
 *****************************************************************************************
 */
#include <string.h>
#include "gh_rpccore.h"

#include "app_thread.h"
#include "health.h"
#include "osal.h"
#include "osal_psram.h"
#include "osal_event.h"
#include "osal_priority_queue.h"
#include "app_log.h"
#include "ble_log.h"

#if GH_USER_LOG_EN
#define DEBUG_LOG(...)                      GH_LOG_LVL_DEBUG(__VA_ARGS__)
#define WARNING_LOG(...)                    GH_LOG_LVL_WARNING(__VA_ARGS__)
#define ERROR_LOG(...)                      GH_LOG_LVL_ERROR(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#define WARNING_LOG(...)
#define ERROR_LOG(...)
#endif


/*
 * DEFINES
 *****************************************************************************************
 */

#define GH_RPC_THREAD_STACK_SIZE    512
#define GH_RPC_THREAD_PRIORITY      10

#define GH_EVENT_TYPE_DATA_RECEIVE  0x00
#define GH_EVENT_TYPE_DATA_SEND     0x01

/*
 * LOCAL VARIABLE DEFINITIONS
 *****************************************************************************************
 */
static osal_task_handle_t  gh_rpc_thread_handle = NULL;
static osal_priority_queue_handle_t gh_priority_queue = NULL;
static osal_mutex_handle_t gh_protocal_mutex = NULL;

/*
 * LOCAL FUNCTION DECLARATION
 *****************************************************************************************
 */
void gh_rpc_thread(void *arg);

/*
 * LOCAL FUNCTION DEFINITIONS
 *****************************************************************************************
 */
void gh_protocal_lock(void)
{
    osal_mutex_take(gh_protocal_mutex, OSAL_MAX_DELAY);
}

void gh_protocal_unlock(void)
{
    osal_mutex_give(gh_protocal_mutex);
}

void gh_protocol_delay()
{
    osal_task_delay(10);
}

uint8_t gh_protocol_data_priority(uint8_t *data, int32_t size)
{
    if (data == NULL || size <= 6)
    {
        return 0;
    }
    if ((data[0] == 0xAA) &&
        (data[1] == 0x11) &&
        (data[3] == 0x9A) &&
        (data[4] == 0x47) &&
        (data[5] == 0x5D))
    {
        return OSAL_EVENT_PRIORITY_LOW;
    }
    else
    {
        return OSAL_EVENT_PRIORITY_HIGH;
    }
}

void gh_protocol_data_send(void *data, int32_t size)
{
    if (data == NULL || size <= 6)
    {
        ERROR_LOG("Invalid data or size");
        return;
    }

    uint8_t priority = gh_protocol_data_priority((uint8_t*)data, size);
    
    osal_event_t *event = osal_event_create(GH_EVENT_TYPE_DATA_SEND, priority, (uint8_t*)data, size);
//    ERROR_LOG("Send size = %d", size);
//    APP_LOG_HEX_DUMP(data, size);
    if (event == NULL)
    {
        ERROR_LOG("Failed to create event");
        return;
    }

    int32_t ret = osal_priority_queue_send(gh_priority_queue, event);
    if (ret != OSAL_SUCCESS)
    {
        ERROR_LOG("Failed to send event to priority queue");
        osal_event_destroy(event);
    }
}

void gh_protocol_data_recevice(uint8_t *p_rx_buffer, uint8_t rx_len)
{
    if (p_rx_buffer == NULL || rx_len == 0)
    {
        ERROR_LOG("Invalid buffer or length");
        return;
    }

    /* DEBUG: log received command bytes (single line) */
    {   char hx[140]; int p = 0;
        p += sprintf(hx + p, "[RX%02d]", rx_len);
        for (uint8_t i = 0; i < rx_len && i < 32 && p < 135; i++)
            p += sprintf(hx + p, "%02X", p_rx_buffer[i]);
        if (rx_len > 32) { hx[p++] = '.'; hx[p++] = '.'; }
        hx[p] = 0;
        ble_printf("%s\n", hx);
    }

    osal_event_t *event = osal_event_create(GH_EVENT_TYPE_DATA_RECEIVE, OSAL_EVENT_PRIORITY_HIGH, p_rx_buffer, rx_len);
    if (event == NULL)
    {
        ERROR_LOG("Failed to create event");
        return;
    }

    int32_t ret = osal_priority_queue_send(gh_priority_queue, event);
    if (ret != OSAL_SUCCESS)
    {
        ERROR_LOG("Failed to send event to priority queue");
        osal_event_destroy(event);
    }
}

static int32_t gh_protocol_send_with_retry(osal_event_t *event)
{
    if (event == NULL || event->data == NULL)
    {
        return OSAL_INVALID_POINTER;
    }

    int32_t ret = health_tx_data_send(0, event->data, event->data_size);
//    APP_LOG_HEX_DUMP(event->data, event->data_size);
    if (ret != 0)
    {
        event->retry_count++;
        if (event->retry_count < OSAL_EVENT_MAX_RETRY_COUNT)
        {
            WARNING_LOG("Send failed, retry %d/%d", event->retry_count, OSAL_EVENT_MAX_RETRY_COUNT);
            osal_task_delay_ms(10);
            ret = osal_priority_queue_send_front(gh_priority_queue, event);
            if (ret == OSAL_SUCCESS)
            {
                return OSAL_SUCCESS;
            }
        }
        ERROR_LOG("Send failed after %d retries", event->retry_count);
        osal_event_destroy(event);
        return OSAL_ERROR;
    }
    
//    DEBUG_LOG("Send success, size: %u", event->data_size);
    osal_event_destroy(event);
    return OSAL_SUCCESS;
}

void gh_protocol_init(void)
{
    int32_t ret = osal_psram_init();
    if (ret != OSAL_SUCCESS)
    {
        ERROR_LOG("Failed to initialize PSRAM");
        return;
    }

    ret = osal_task_create("gh_rpc", gh_rpc_thread, GH_RPC_THREAD_STACK_SIZE, GH_RPC_THREAD_PRIORITY, &gh_rpc_thread_handle);
    if (ret != OSAL_SUCCESS)
    {
        ERROR_LOG("Failed to create gh_rpc_thread");
        return;
    }

    GhRPCInitialInfo info;

    memset(&info, 0, sizeof(GhRPCInitialInfo));
    info.lock = gh_protocal_lock;
    info.unlock = gh_protocal_unlock;
    info.delay = gh_protocol_delay;
    info.sendFunction = gh_protocol_data_send;
    GHRPC_init(info);

    DEBUG_LOG("GHRPC_init ok!");
}

void gh_rpc_thread(void *arg) 
{
    int32_t ret = osal_priority_queue_create(&gh_priority_queue);
    if (ret != OSAL_SUCCESS)
    {
        ERROR_LOG("Failed to create priority queue");
        return;
    }
    
    ret = osal_mutex_create(&gh_protocal_mutex);
    if (ret != OSAL_SUCCESS)
    {
        ERROR_LOG("Failed to create gh_protocal_mutex");
        osal_priority_queue_delete(gh_priority_queue);
        return;
    }

    DEBUG_LOG("GH RPC thread started, waiting for events...");

    while (1) {
        osal_event_t *event = NULL;
        ret = osal_priority_queue_receive(gh_priority_queue, &event, OSAL_MAX_DELAY);
        if (ret != OSAL_SUCCESS || event == NULL)
        {
            ERROR_LOG("Failed to receive event from priority queue");
            continue;
        }

//        DEBUG_LOG("Received event: type=0x%04X, priority=%d, size=%u, high_count=%u, low_count=%u",
//                  event->event_type, event->priority, event->data_size,
//                  osal_priority_queue_high_count(gh_priority_queue),
//                  osal_priority_queue_low_count(gh_priority_queue));

        switch (event->event_type)
        {
        case GH_EVENT_TYPE_DATA_RECEIVE:
            GHRPC_process(event->data, event->data_size, 0);
            osal_event_destroy(event);
            break;
        case GH_EVENT_TYPE_DATA_SEND:
            gh_protocol_send_with_retry(event);
            break;
        default:
            ERROR_LOG("Received unexpected event type: 0x%04X", event->event_type);
            osal_event_destroy(event);
            break;
        }
    }
}
