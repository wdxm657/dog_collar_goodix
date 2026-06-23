/**
 *****************************************************************************************
 *
 * @file main.c
 *
 * @brief main function Implementation.
 *
 *****************************************************************************************
 * @attention
  #####Copyright (c) 2019 GOODIX
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
 *****************************************************************************************
 */

/*
 * INCLUDE FILES
 *****************************************************************************************
 */
#include "user_app.h"
#include "user_periph_setup.h"
#include "gr_includes.h"
#include "scatter_common.h"
#include "flash_scatter_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "watcher.h"
#include "custom_config.h"
#include "patch.h"
#include "app_log.h"
#include "pmu_calibration.h"
#include "app_rtc.h"
#include "dfu_port.h"
#include "board_SK.h"

#include "app_thread.h"
/*
 * DEFINES
 *****************************************************************************************
 */

#define LOG_STORE_DUMP_TASK_STACK_SIZE  ( 512 )//unit : word

#ifdef SOC_GR5515
#define DFU_TASK_STACK_SIZE             ( 1024 * 2 )//unit : word
#else
#define DFU_TASK_STACK_SIZE             ( 256 )//unit : word
#endif

/*
 * LOCAL VARIABLE DEFINITIONS
 *****************************************************************************************
 */
/**@brief Stack global variables for Bluetooth protocol stack. */
STACK_HEAP_INIT(heaps_table);
calendar_time_t g_calendar_time;
static uint32_t s_app_rtc_tick_period_ms = 1000;

/*
 * LOCAL FUNCTION DEFINITIONS
 ****************************************************************************************
 */
static SemaphoreHandle_t dfu_running_semaphore = NULL;
static SemaphoreHandle_t dfu_break_semaphore = NULL;

void dfu_status_set(uint8_t status)
{
    if (status == 0x01) {
        if (dfu_running_semaphore != NULL) {
            xSemaphoreGive(dfu_running_semaphore);
        }
    } else if (status == 0x02) {
        if (dfu_break_semaphore != NULL) {
            xSemaphoreGive(dfu_break_semaphore);
        }
    }
}

static void dfu_schedule_task(void *p_arg)
{
    if (dfu_running_semaphore == NULL) {
        dfu_running_semaphore = xSemaphoreCreateBinary();
    }
    if (dfu_break_semaphore == NULL) {
        dfu_break_semaphore = xSemaphoreCreateBinary();
    }
    while (1)
    {
        if (xSemaphoreTake(dfu_running_semaphore, portMAX_DELAY) == pdTRUE)
        {
            while (1)
            {
                dfu_schedule();
                vTaskDelay(10);
                xSemaphoreTake(dfu_running_semaphore, 0);
                if (xSemaphoreTake(dfu_break_semaphore, 0) == pdTRUE)
                {
                    break;
                }
            }
        }
    }
}

#if APP_LOG_STORE_ENABLE
static void log_store_dump_task(void *p_arg)
{
    while (1)
    {
        app_log_store_schedule();
    }
}
#endif

extern uint64_t bsp_timestamp_get(void);

#include "gh_demo.h"
#include "ble_log.h"

/*
 * ADT 佩戴监控任务: 检测到戴上自动开启 HR+HRV, 摘下自动关闭
 */
#define ADT_MONITOR_STACK_SIZE 256

static void adt_wear_monitor_task(void *p_arg)
{
    uint32_t prev_event = 0;
    uint8_t hr_hrv_active = 0;

    while (1)
    {
        uint32_t evt = g_adt_wear_event;

        if (evt != prev_event)
        {
            if (evt == 1 && hr_hrv_active == 0)   /* wear_on → 开启 HR+HRV */
            {
                ble_printf("[ADT] wear_on -> start HR+HRV\n");
                health_start_event_send(HEALTH_MODE_HR | HEALTH_MODE_HRV, false);
                hr_hrv_active = 1;
            }
            else if (evt == 2 && hr_hrv_active == 1)  /* wear_off → 关闭 HR+HRV */
            {
                ble_printf("[ADT] wear_off -> stop HR+HRV\n");
                health_stop_event_send(HEALTH_MODE_HR | HEALTH_MODE_HRV, false);
                hr_hrv_active = 0;
            }
            prev_event = evt;
        }
        vTaskDelay(pdMS_TO_TICKS(200));  /* 200ms 轮询一次 */
    }
}

static void app_apply_rtc_tick_period(uint32_t tick_ms)
{
    s_app_rtc_tick_period_ms = tick_ms;
    app_rtc_setup_tick(s_app_rtc_tick_period_ms);
}

static void app_rtc_evt_handler(app_rtc_evt_t *p_evt)
{
    if (p_evt->type == APP_RTC_EVT_TICK_ALARM)
    {
        app_mqueue_send_event(MQ_EVENT_RTC_TICK, OSAL_NO_WAIT);
//        APP_LOG_INFO("timestamp: %d", bsp_timestamp_get());
    }
}

static void app_calendar_init(void)
{
    g_calendar_time.year = 21;
    g_calendar_time.mon  = 12;
    g_calendar_time.date = 1;
    g_calendar_time.hour = 1;
    g_calendar_time.min  = 00;
    g_calendar_time.sec  = 00;
    app_rtc_init(app_rtc_evt_handler);
    app_rtc_init_time(&g_calendar_time);
    
    app_rtc_setup_tick(s_app_rtc_tick_period_ms);
}

void app_set_rtc_tick_period(uint32_t tick_ms)
{
    if (tick_ms == 0)
    {
        return;
    }

    app_thread_set_tick_period_ms(tick_ms);
}

uint32_t app_get_rtc_tick_period(void)
{
    return app_thread_get_tick_period_ms();
}

uint32_t app_get_pending_rtc_tick_period(void)
{
    return app_thread_get_pending_tick_period_ms();
}

uint32_t app_get_phas_tick_acc_ms(void)
{
    return app_thread_get_phas_tick_acc_ms();
}

/**
 *****************************************************************************************
 * @brief To create two task, the one is ble-schedule, another is watcher task
 *****************************************************************************************
 */
static void vStartTasks(void *arg)
{
    app_thread_register_tick_apply_hook(app_apply_rtc_tick_period);
    app_thread_set_tick_period_ms(s_app_rtc_tick_period_ms);
    app_calendar_init();
    app_thread_init();
    xTaskCreate(dfu_schedule_task, "dfu_schedule_task", DFU_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 2, NULL);
#if APP_LOG_STORE_ENABLE
    xTaskCreate(log_store_dump_task, "log_store_dump_task", LOG_STORE_DUMP_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 3, NULL);
#endif
    xTaskCreate(adt_wear_monitor_task, "adt_monitor", ADT_MONITOR_STACK_SIZE, NULL, configMAX_PRIORITIES - 4, NULL);
    vTaskDelete(NULL);
}

/**
 *****************************************************************************************
 * @brief main function
 *****************************************************************************************
 */
int main(void)
{
    app_periph_init();                                              /*<init user periph .*/
    ble_stack_init(ble_evt_handler, &heaps_table);                  /*< init ble stack*/

    xTaskCreate(vStartTasks, "create_task", 512, NULL, 0, NULL);    /*< create some demo tasks via freertos */
    vTaskStartScheduler();                                          /*< freertos run all tasks*/
    for (;;);                                                       /*< Never perform here */
}

/*
 * =============================================================================
 * Key Mapping & Function Definition
 * =============================================================================
 * SW1 : AON_GPIO_5  ->  BSP_KEY_UP_ID
 * SW2 : AON_GPIO_6  ->  BSP_KEY_DOWN_ID
 * SW3 : AON_GPIO_7  ->  BSP_KEY_OK_ID
 *
 * Key Function Table:
 * -----------------------------------------------------------------------------
 * Key ID           | Single Click      | Double Click      | Long Press
 * -----------------|-------------------|-------------------|-------------------
 * BSP_KEY_UP_ID    | Start HR/HRV      | Stop HR/HRV       | -
 * BSP_KEY_DOWN_ID  | Start SPO2        | Stop SPO2         | -
 * BSP_KEY_OK_ID    | Start TEST1       | Stop TEST1        | Simulate ACC movement
 * =============================================================================
 */
extern uint8_t active_move_flag;
void app_key_evt_handler(uint8_t key_id, app_key_click_type_t key_click_type)
{
    
    switch (key_id){
        case BSP_KEY_UP_ID: {
            if (APP_KEY_SINGLE_CLICK == key_click_type) {
                health_start_event_send(HEALTH_MODE_HR|HEALTH_MODE_HRV, false);
            } else if (APP_KEY_DOUBLE_CLICK == key_click_type) {
                health_stop_event_send(HEALTH_MODE_HR|HEALTH_MODE_HRV, false);
            } 
        } break;

        case BSP_KEY_DOWN_ID: {
            if (APP_KEY_SINGLE_CLICK == key_click_type) {
                health_start_event_send(HEALTH_MODE_SPO2, false);
            } else if (APP_KEY_DOUBLE_CLICK == key_click_type) {
                health_stop_event_send(HEALTH_MODE_SPO2, false);
            } 
        } break;
        
        case BSP_KEY_OK_ID: {
            if (APP_KEY_SINGLE_CLICK == key_click_type) {
                health_start_event_send(HEALTH_MODE_TEST1, false);
            } else if (APP_KEY_DOUBLE_CLICK == key_click_type) {
                health_stop_event_send(HEALTH_MODE_TEST1, false);
            } else if (APP_KEY_LONG_CLICK == key_click_type) {
                active_move_flag = 2;
            }
            
        } break;
        default: break;
    }
}

