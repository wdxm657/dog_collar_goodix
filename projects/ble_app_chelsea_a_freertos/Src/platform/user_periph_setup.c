/**
 *****************************************************************************************
 *
 * @file user_periph_setup.c
 *
 * @brief  User Periph Init Function Implementation.
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
#include "app_log.h"
#include "user_periph_setup.h"
#include "gr_includes.h"
#include "dfu_port.h"
#include "board_SK.h"
#include "hal_flash.h"
#include "app_rtc.h"
#include "flash_scatter_config.h"
#include "FreeRTOS.h"
#include "Semphr.h"
#include <inttypes.h>
/*
 * LOCAL VARIABLE DEFINITIONS
 *****************************************************************************************
 */
/**@brief Bluetooth device address. */
//static const uint8_t  s_bd_addr[SYS_BD_ADDR_LEN] = {0x11, 0x00, 0xcf, 0x3e, 0xcb, 0xea};

#ifdef SOC_GR533X
#define DFU_FW_SAVE_ADDR       (FLASH_START_ADDR + 0x40000)
#else
#define DFU_FW_SAVE_ADDR       (FLASH_START_ADDR + 0x60000)
#endif

/*
 * GLOBAL FUNCTION DEFINITIONS
 *****************************************************************************************
 */
#if APP_LOG_STORE_ENABLE
static SemaphoreHandle_t s_app_log_sem;

static void rtc_time_get(app_log_store_time_t *time)
{
    calendar_time_t calendar_time;
    app_rtc_get_time(&calendar_time);
    
    uint8_t *p_time = (uint8_t *)time;
    p_time[0] = calendar_time.year;
    p_time[1] = calendar_time.mon;
    p_time[2] = calendar_time.date;
    p_time[3] = calendar_time.hour;
    p_time[4] = calendar_time.min;
    p_time[5] = calendar_time.sec;
    p_time[6] = (calendar_time.ms >> 0) & 0xFF;
    p_time[7] = (calendar_time.ms >> 8) & 0xFF;
}

static void app_log_sem_give(void)
{
    if (s_app_log_sem)
    {
        if (__get_IPSR())
        {
            xSemaphoreGiveFromISR(s_app_log_sem, NULL);                 //Nedd add portYIELD_FROM_ISR ?
        }
        else
        {
            xSemaphoreGive(s_app_log_sem);
        }
    }
}

static void app_log_sem_take(void)
{
    if (s_app_log_sem)
    {
        xSemaphoreTake(s_app_log_sem, portMAX_DELAY);
    }
}

static void log_store_init(void)
{
    app_log_store_info_t store_info;
    app_log_store_op_t   op_func;

    store_info.nv_tag   = 0x40ff;
    store_info.db_addr  = FLASH_START_ADDR + 0x60000;;
    store_info.db_size  = 0x20000;
    store_info.blk_size = 0x1000;

    op_func.flash_init  = hal_flash_init;
    op_func.flash_erase = hal_flash_erase;
    op_func.flash_write = hal_flash_write;
    op_func.flash_read  = hal_flash_read;
    op_func.time_get    = rtc_time_get;
    op_func.sem_give    = app_log_sem_give;
    op_func.sem_take    = app_log_sem_take;

    app_log_store_init(&store_info, &op_func);
}
#endif


void app_periph_init(void)
{
//    SYS_SET_BD_ADDR(s_bd_addr);
    board_init();
    dfu_port_init(NULL, DFU_FW_SAVE_ADDR, NULL);

#if APP_LOG_STORE_ENABLE
    log_store_init();
#endif

    pwr_mgmt_mode_set(PMR_MGMT_SLEEP_MODE);
}

extern uint32_t gh_strlen(const char *sz_src);
extern void gh_app_demo_read_efuse(uint64_t efuse_256bit[4]);
void gh_chip_version_get(uint8_t* pchVer, size_t* size, size_t max_size)
{
    uint8_t device_uid[16];
    sys_device_uid_get(device_uid);
    snprintf((char*)pchVer, max_size-1, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", 
                                device_uid[0],  device_uid[1],  device_uid[2],  device_uid[3], 
                                device_uid[4],  device_uid[5],  device_uid[6],  device_uid[7],
                                device_uid[8],  device_uid[9],  device_uid[10], device_uid[11],
                                device_uid[12], device_uid[13], device_uid[14], device_uid[15]
                            );
    *size = gh_strlen((const char *)pchVer) + 1;
}

void gh_ble_version_get(uint8_t* pchVer, size_t* size, size_t max_size)
{
    ble_gap_bdaddr_t  bd_addr;
    ble_gap_addr_get(&bd_addr);
    uint64_t efuse_256bit[4];
    
    gh_app_demo_read_efuse(efuse_256bit);
    
    snprintf((char*)pchVer, max_size-1, "%016" PRIX64 "-%016" PRIX64 "-%016" PRIX64 "-%016" PRIX64 ".%02X:%02X:%02X:%02X:%02X:%02X", 
    efuse_256bit[0], efuse_256bit[1], efuse_256bit[2], efuse_256bit[3],
    bd_addr.gap_addr.addr[0], bd_addr.gap_addr.addr[1], 
    bd_addr.gap_addr.addr[2], bd_addr.gap_addr.addr[3], 
    bd_addr.gap_addr.addr[4], bd_addr.gap_addr.addr[5]);
    *size = gh_strlen((const char *)pchVer) + 1;
}

//void GH3X_GetVersion(uint8_t uchVerType,int8_t* pchVer, size_t* size)
//{
//    APP_LOG_INFO("uchVerType = %d", uchVerType);
//    gh_ble_version_get((uint8_t*)pchVer, size, 150);
//    APP_LOG_INFO("size = %d", *size);
//}

