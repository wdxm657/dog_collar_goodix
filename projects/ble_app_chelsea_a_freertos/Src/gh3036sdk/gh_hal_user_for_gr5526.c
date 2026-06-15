/**
  ****************************************************************************************
  * @file    gh_hal_user.c
  * @author  GHealth Driver Team
  * @brief   goodix hal user
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
#include <stdint.h>
#include "gh_hal_log.h"
#include "gh_hal_config.h"
#include "gh_hal_service.h"
#if (1 == GH_USE_SDK_APP)
#include "gh_app.h"
#endif
#include "gh_hal_user.h"

#include "gh_hal_io_config_for_gr5526.h"

#include "app_thread.h"

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
/* isr event name */

static osal_mutex_handle_t gh_timestamp_mutex = NULL;

#if GH_LOG_DEBUG_ENABLE
//static const char* g_isr_event_name[GH_HAL_ISR_MAX] =
//{
//    "CHIP_RESET",
//    "FIFO_UP",
//    "FIFO_DOWN",
//    "FIFO_WATER",
//    "TIMER",
//    "USER",
//    "FRAME_DONE",
//    "SAMPLE_ERROR",
//    "CAP_CANCEL",
//    "LDO_OC",
//    "SYNC_SAMPLE_ERR"
//};
#endif

#if (0 == GH_USE_SDK_APP)
/* data type */
static const char* g_data_type[] =
{
    "GH_PPG_DATA",
    "GH_PPG_MIX_DATA",
    "GH_PPG_BG_DATA",
    "GH_PPG_BG_CANCEL",
    "GH_PPG_LED_DRV",
    "GH_ECG_DATA",
    "GH_BIA_DATA",
    "GH_GSR_DATA",
    "GH_PRESSURE_DATA",
    "GH_TEMPERATURE_DATA",
    "GH_CAP_DATA",
    "GH_PPG_PARAM_DATA",
    "GH_PPG_DRE_DATA",
};
#endif

/*
 * GLOBAL VARIABLE DEFINITIONS
 *****************************************************************************************
 */
osal_mutex_handle_t gh_hal_mutex = NULL;
/*
 * LOCAL FUNCTION DEFINITIONS
 *****************************************************************************************
 */
void gh_hal_mutex_init(void)
{
    osal_mutex_create(&gh_hal_mutex);
}

void gh_hal_mutex_lock(void)
{
    osal_mutex_take(gh_hal_mutex, OSAL_MAX_DELAY);
}

void gh_hal_mutex_unlock(void)
{
    osal_mutex_give(gh_hal_mutex);
}
 
void calendar_time2seconds(calendar_time_t *p_time, uint32_t *p_seconds)
{
    uint16_t year;
    uint32_t utc;

    // 10957 is the days between 1970/1/1 and 2000/1/1
    year = (p_time->year + 2000) % 100;
    utc  = 10957;
    utc += (year * 365 + (year + 3) / 4);
    utc += (367 * p_time->mon - 362) / 12 - (p_time->mon <= 2 ? 0 : ((year % 4 == 0) ? 1 : 2));
    utc += (p_time->date - 1);
    utc *= 86400;
    utc += (p_time->hour * 3600 + p_time->min * 60 + p_time->sec);

    *p_seconds = utc;
}

uint64_t bsp_timestamp_get(void)
{
    calendar_time_t calendar_time;
    uint32_t seconds;
    uint64_t ret;
    if (gh_timestamp_mutex) {
        osal_mutex_take(gh_timestamp_mutex, OSAL_MAX_DELAY);
    }
    app_rtc_get_time(&calendar_time);

    calendar_time2seconds(&calendar_time, &seconds);

    ret = (uint64_t)seconds * 1000 + calendar_time.ms;
    if (gh_timestamp_mutex) {
        osal_mutex_give(gh_timestamp_mutex);
    }
    return ret;
}
uint64_t gh_hal_get_timestamp(void)
{
    // return bsp_timestamp_get();
    return bsp_timestamp_get();
}

uint32_t gh_hal_delay_ms(uint16_t ms)
{
    delay_ms(ms);
    return 0;
}

uint32_t gh_hal_delay_us(uint16_t us)
{
    delay_us(us);
    return 0;
}

int gh_hal_log_user(char *str)
{
    printf("%s\r\n", str);
    return 0;
}

#if (GH_USE_STD_SNPRINTF == 0)
int gh_hal_snprintf_user(char *p_str, size_t size, const char *p_format, ...)
{
    // return bsp_snprintf(p_str, size, p_format);
    return 0;
}
#endif

extern uint8_t gh_hal_get_interface_type(void);

app_spi_params_t gh_spi_params = GH_DEFAULT_PARAM_CONFIG;

volatile uint8_t g_master_tdone = 0;
volatile uint8_t g_master_rdone = 0;
        
static void gh_spi_master_callback(app_spi_evt_t *p_evt)
{
    if (p_evt->type == APP_SPI_EVT_TX_CPLT)
    {
        g_master_tdone = 1;
    }
    if (p_evt->type == APP_SPI_EVT_RX_CPLT)
    {
        g_master_rdone = 1;
    }
    if (p_evt->type == APP_SPI_EVT_TX_RX_CPLT)
    {
        g_master_tdone = 1;
        g_master_rdone = 1;
    }
    if (p_evt->type == APP_SPI_EVT_ERROR)
    {
        g_master_tdone = 1;
        g_master_rdone = 1;
    }
}

#if defined(CHEALSE_A_SOFT_SPI) && (CHEALSE_A_SOFT_SPI == 1)
spi_cfg spi0;
void gh_hal_soft_spi_mosi_ctrl(uint8_t level)
{
    app_io_write_pin(GH_MOSI_IO_TYPE, GH_MOSI_PIN, level ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
}

void gh_hal_soft_spi_clk_ctrl(uint8_t level)
{
    app_io_write_pin(GH_CLK_IO_TYPE, GH_CLK_PIN, level ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
}

uint8_t gh_hal_soft_spi_miso_ctrl(void)
{
    return app_io_read_pin(GH_MISO_IO_TYPE, GH_MISO_PIN);
}

#endif

uint32_t gh_hal_spi_init(void)
{
#if defined(CHEALSE_A_SOFT_SPI) && (CHEALSE_A_SOFT_SPI == 1)
    app_io_init_t io_init = APP_IO_DEFAULT_CONFIG;

    io_init.pull = APP_IO_PULLUP;
    io_init.mode = APP_IO_MODE_OUTPUT;
    io_init.pin  = GH_MOSI_PIN;
    io_init.mux  = APP_IO_MUX;
    app_io_init(GH_MOSI_IO_TYPE, &io_init);
    
    io_init.pull = APP_IO_PULLUP;
    io_init.mode = APP_IO_MODE_OUTPUT;
    io_init.pin  = GH_CLK_PIN;
    io_init.mux  = APP_IO_MUX;
    app_io_init(GH_CLK_IO_TYPE, &io_init);
    
    io_init.pull = APP_IO_PULLUP;
    io_init.mode = APP_IO_MODE_OUTPUT;
    io_init.pin  = GH_CS_PIN;
    io_init.mux  = APP_IO_MUX;
    app_io_init(GH_CS_IO_TYPE, &io_init);
    
    io_init.pull = APP_IO_PULLUP;
    io_init.mode = APP_IO_MODE_INPUT;
    io_init.pin  = GH_MISO_PIN;
    io_init.mux  = APP_IO_MUX;
    app_io_init(GH_MISO_IO_TYPE, &io_init);
    
    soft_spi_init(&spi0, 0, 0, gh_hal_soft_spi_clk_ctrl, gh_hal_soft_spi_mosi_ctrl, gh_hal_soft_spi_miso_ctrl);
    
#else
    app_drv_err_t ret = 0;
    ret = app_spi_init(&gh_spi_params, gh_spi_master_callback);
    if (ret != 0)
    {
        ERROR_LOG("SPI master initial failed! Please check the input paraments.\r\n");
        return ret;
    }

    ret = app_spi_dma_init(&gh_spi_params);
    if (ret != 0)
    {
        ERROR_LOG("SPI master dma initial failed! Please check the input paraments.\r\n");
        return ret;
    }


    if (gh_hal_get_interface_type() == GH_INTERFACE_SPI_SW_CS)
    {
        
        app_io_init_t io_init = APP_IO_DEFAULT_CONFIG;

        io_init.pull = APP_IO_PULLUP;
        io_init.mode = APP_IO_MODE_OUTPUT;
        io_init.pin  = GH_CS_PIN;
        io_init.mux  = GH_CS_PINMUX;
        app_io_init(GH_CS_IO_TYPE, &io_init);
    }
    
#endif
    DEBUG_LOG("SPI master initial success!\r\n");
    if (gh_timestamp_mutex == NULL)
    {
        osal_mutex_create(&gh_timestamp_mutex);
    }

    return 0;
}

uint32_t gh_hal_spi_deinit(void)
{
#if defined(CHEALSE_A_SOFT_SPI) && (CHEALSE_A_SOFT_SPI == 1)


    app_io_deinit(GH_MOSI_IO_TYPE, GH_MOSI_PIN);
    
    app_io_deinit(GH_CLK_IO_TYPE, GH_CLK_PIN);
    
    app_io_deinit(GH_CS_IO_TYPE, GH_CS_PIN);
    
    app_io_deinit(GH_MISO_IO_TYPE, GH_MISO_PIN);
    
    // soft_spi_init(&spi0, 0, 0, gh_hal_soft_spi_clk_ctrl, gh_hal_soft_spi_mosi_ctrl, gh_hal_soft_spi_miso_ctrl);
    
#else
    app_drv_err_t ret = 0;
    ret = app_spi_deinit(APP_SPI_ID_MASTER);
    if (ret != 0)
    {
        ERROR_LOG("SPI master deinit failed! Please check the input paraments.\r\n");
        return ret;
    }

    ret = app_spi_dma_deinit(APP_SPI_ID_MASTER);
    if (ret != 0)
    {
        ERROR_LOG("SPI master dma deinit failed! Please check the input paraments.\r\n");
        return ret;
    }


    if (gh_hal_get_interface_type() == GH_INTERFACE_SPI_SW_CS)
    {
        app_io_deinit(GH_CS_IO_TYPE, GH_CS_PIN);
    }

    
#endif
    DEBUG_LOG("SPI master deinit successfull!\r\n");
    return 0;
}



uint32_t gh_hal_spi_write(uint8_t* buffer, uint16_t len)
{
    uint32_t ret = 0;
#if defined(CHEALSE_A_SOFT_SPI) && (CHEALSE_A_SOFT_SPI == 1)
    soft_spi_send(&spi0, buffer, len);
#else
    g_master_tdone = 0;
    ret = app_spi_dma_transmit_async(APP_SPI_ID_MASTER, buffer, len);
    if (ret)
    {
        ERROR_LOG("spi write error. ret = %d.\r\n", ret);
        return ret;
    }
    while (!g_master_tdone);
#endif
    return ret;
}

#if (GH_INTERFACE_TYPE == GH_INTERFACE_SPI_SW_CS)
uint32_t gh_hal_spi_read(uint8_t* buffer, uint16_t len)
{
    uint32_t ret = 0;
#if defined(CHEALSE_A_SOFT_SPI) && (CHEALSE_A_SOFT_SPI == 1)
    soft_spi_recv(&spi0, buffer, len);
#else
    g_master_rdone = 0;
    ret = app_spi_dma_receive_async(APP_SPI_ID_MASTER, buffer, len);
    if (ret)
    {
        ERROR_LOG("spi read error, err = %d \r\n", ret);
        return ret;
    }
    while (!g_master_rdone);
#endif
    return ret;
}

uint32_t gh_hal_spi_cs_ctrl(uint8_t level)
{
    app_io_write_pin(GH_CS_IO_TYPE, GH_CS_PIN, level ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
    return 0;
}
#endif

uint32_t gh_hal_spi_write_read(uint8_t* tx_buffer, uint8_t* rx_buffer, uint16_t len)
{
    // bsp_spi_write_read(tx_buffer, rx_buffer, len);
    return 0;
}


app_i2c_params_t gh_i2c_params = GH_DEFAULT_I2C_PARAM_CONFIG;
uint32_t gh_hal_i2c_init(void)
{

    app_io_init_t io_init = APP_IO_DEFAULT_CONFIG;

    io_init.pull = APP_IO_PULLUP;
    io_init.mode = APP_IO_MODE_OUTPUT;
    io_init.pin  = GH_CS_PIN;
    io_init.mux  = GH_CS_PINMUX;
    app_io_init(GH_CS_IO_TYPE, &io_init);

    io_init.pin  = GH_MISO_PIN;
    io_init.mux  = APP_IO_MUX;
    app_io_init(GH_MISO_IO_TYPE, &io_init);

    app_io_write_pin(GH_CS_IO_TYPE, GH_CS_PIN, APP_IO_PIN_RESET);
    app_io_write_pin(GH_MISO_IO_TYPE, GH_MISO_PIN, APP_IO_PIN_RESET);

	if (app_i2c_init(&gh_i2c_params, NULL) != 0)
	{
		ERROR_LOG("i2c initial failed! Please check the input paraments.\r\n");
		return 1;
    }
    DEBUG_LOG("i2c initial successfully!\r\n");
    if (gh_timestamp_mutex == NULL)
    {
        osal_mutex_create(&gh_timestamp_mutex);
    }
    return 0;
}

uint32_t gh_hal_i2c_deinit(void)
{
    app_i2c_deinit(GH_I2C_ID);
    app_io_deinit(GH_CS_IO_TYPE, GH_CS_PIN);
    app_io_deinit(GH_MISO_IO_TYPE, GH_MISO_PIN);
    return 0;
}


uint32_t gh_hal_i2c_write(uint8_t i2c_slaver_id, uint8_t* p_buffer, uint16_t len)
{
    uint8_t ret = 0;

    ret = app_i2c_transmit_sync(GH_I2C_ID, (i2c_slaver_id >> 1), (uint8_t *) p_buffer, len, 100);
	if (ret)
	{
		ERROR_LOG("i2c(%X) transmit failed(%d)! \r\n",i2c_slaver_id, ret);
		return ret;
	}
    return ret;
}

uint32_t gh_hal_i2c_read(uint8_t i2c_slaver_id, uint8_t* p_buffer, uint16_t len)
{
    uint8_t ret = 0;

    ret = app_i2c_receive_sync(GH_I2C_ID, (i2c_slaver_id >> 1), (uint8_t *) p_buffer, len, 100);
	if (ret)
	{
		ERROR_LOG("i2c(%X) app_i2c_receive_sync failed(%d)! \r\n",i2c_slaver_id, ret);
		return ret;
	}
    return ret;
}


#if (GH_ISR_MODE == INTERRUPT_MODE)

static void gh_int_callback(app_io_evt_t *p_evt)
{
    app_mqueue_send_event(MQ_EVENT_HEALTH_INT_UPDATE, OSAL_NO_WAIT);
}

uint32_t gh_hal_int_pin_init(void)
{
    app_gpiote_param_t gh3x2x_int_param = {
        GH_INT_IO_TYPE, GH_INT_PIN, APP_IO_MODE_IT_RISING, APP_IO_PULLUP, gh_int_callback
    };
    app_gpiote_init(&gh3x2x_int_param, 1);
    DEBUG_LOG("[%s] init ok\r\n", __FUNCTION__);
    return 0;
}
#endif

#if (GH_SUPPORT_HARD_RESET)
uint32_t gh_hal_reset_pin_init(void)
{
    app_io_init_t io_init = APP_IO_DEFAULT_CONFIG;

    io_init.pull = APP_IO_PULLUP;
    io_init.mode = APP_IO_MODE_OUTPUT;
    io_init.pin  = GH_RST_PIN;
    io_init.mux  = GH_RST_PINMUX;
    app_io_init(GH_RST_IO_TYPE, &io_init);

    app_io_write_pin(GH_RST_IO_TYPE, GH_RST_PIN, APP_IO_PIN_RESET);
    gh_hal_delay_ms(10);
    app_io_write_pin(GH_RST_IO_TYPE, GH_RST_PIN, APP_IO_PIN_SET);
    return 0;
}

uint32_t gh_hal_reset_pin_ctrl(uint8_t level)
{
    app_io_write_pin(GH_RST_IO_TYPE, GH_RST_PIN, level ? APP_IO_PIN_SET : APP_IO_PIN_RESET);
    return 0;
}
#endif

uint32_t gh_hal_isr_event_publish(gh_hal_isr_status_t *p_event)
{
    /* Get interrupt event from here */
    

    return 0;
}

uint32_t gh_hal_fifo_data_publish(uint8_t *p_data, uint16_t size)
{
    /* Get fifo rawadata from here, */
    /* If the customer selects the gsensor synchronization mode, start reading gsensor data */
#if (1 == GH_USE_SDK_APP)
    #if (GH_FUSION_MODE_SEL == GH_FUSION_MODE_SYNC)
    gh_gsensor_ts_and_data_t g_data[25];
    g_data[0].timestamp = gh_hal_get_timestamp();
    int16_t acc = g_data[0].timestamp & 0x7FFF;
    g_data[0].timestamp  -= 40 * 23;
    for (uint8_t i = 0; i < 25; i ++) {
        g_data[i].data.acc[0] = 0;//i;
        g_data[i].data.acc[1] = 0;//acc + i * 40;
        g_data[i].data.acc[2] = 512;//acc - i * 40;
        
        g_data[i].timestamp = g_data[0].timestamp + i * 40;
    }
    gh_demo_gsensor_data_sync_set(g_data, 25);
    #endif
#endif
    return 0;
}


void gh_data_get_callback(gh_data_t *p_gh_data, uint16_t len)
{  
#if (0 == GH_USE_SDK_APP)

    DEBUG_LOG("DATA LEN = %d\r\n", len);

    for (uint16_t i = 0; i < len; i++)
    {
        gh_data_type_e data_type = p_gh_data[i].data_channel.data_type;
        switch (data_type)
        {
        case GH_PPG_DATA:
            DEBUG_LOG("%s, slot:%d, rx:%d, rawdata:%d, ipd_pA:%d\r\n",
                             g_data_type[data_type],
                             p_gh_data[i].ppg_data.data_channel.channel_ppg.slot_cfg_id,
                             p_gh_data[i].ppg_data.data_channel.channel_ppg.rx_id,
                             p_gh_data[i].ppg_data.rawdata,
                             p_gh_data[i].ppg_data.ipd_pa);
            break;

#if GH_SUPPORT_FIFO_CTRL_DEBUG1
        case GH_PPG_MIX_DATA:
            DEBUG_LOG("%s, slot:%d, rx:%d, rawdata:%d, ipd_pA:%d, mix_id:%d\r\n",
                             g_data_type[data_type],
                             p_gh_data[i].ppg_mixdata.data_channel.channel_ppg_mix.slot_cfg_id,
                             p_gh_data[i].ppg_mixdata.data_channel.channel_ppg_mix.rx_id,
                             p_gh_data[i].ppg_mixdata.rawdata,
                             p_gh_data[i].ppg_mixdata.ipd_pa,
                             p_gh_data[i].ppg_mixdata.data_channel.channel_ppg_mix.mix_id);
            break;

        case GH_PPG_BG_DATA:
            DEBUG_LOG("%s, slot:%d, rx:%d, rawdata:%d, ipd_pA:%d, bg_id:%d\r\n",
                             g_data_type[data_type],
                             p_gh_data[i].ppg_bg_data.data_channel.channel_ppg_bg.slot_cfg_id,
                             p_gh_data[i].ppg_bg_data.data_channel.channel_ppg_bg.rx_id,
                             p_gh_data[i].ppg_bg_data.rawdata,
                             p_gh_data[i].ppg_bg_data.ipd_pa,
                             p_gh_data[i].ppg_bg_data.data_channel.channel_ppg_bg.bg_id);
            break;

        case GH_PPG_DRE_DATA:
            DEBUG_LOG("%s, slot:%d, rx:%d, rawdata:%d, dre_update:%d\r\n",
                             g_data_type[data_type],
                             p_gh_data[i].ppg_dre_data.data_channel.channel_ppg_dre.slot_cfg_id,
                             p_gh_data[i].ppg_dre_data.data_channel.channel_ppg_dre.rx_id,
                             p_gh_data[i].ppg_dre_data.rawdata,
                             p_gh_data[i].ppg_dre_data.dre_update);
            break;
#endif

#if GH_SUPPORT_FIFO_CTRL_DEBUG0
        case GH_PPG_PARAM_DATA:
            DEBUG_LOG("%s, slot:%d, rx:%d, dc range:%d, bg range:%d, gain: %d\r\n",
                             g_data_type[data_type],
                             p_gh_data[i].ppg_param_data.data_channel.channel_ppg_param.slot_cfg_id,
                             p_gh_data[i].ppg_param_data.data_channel.channel_ppg_param.rx_id,
                             p_gh_data[i].ppg_param_data.param_rawdata.param.dc_cancel_range,
                             p_gh_data[i].ppg_param_data.param_rawdata.param.bg_cancel_range,
                             p_gh_data[i].ppg_param_data.param_rawdata.param.gain_code);
            DEBUG_LOG("skip:%d, dc_cancel: %d, bg_cancel: %d\r\n",
                             p_gh_data[i].ppg_param_data.param_rawdata.param.skip_ok_flag,
                             p_gh_data[i].ppg_param_data.param_rawdata.param.dc_cancel_code,
                             p_gh_data[i].ppg_param_data.param_rawdata.param.bg_cancel_code);

            break;
#endif
        case GH_CAP_DATA:
            DEBUG_LOG("%s, slot:%d, rawdata:%d\r\n",
                             g_data_type[data_type],
                             p_gh_data[i].cap_data.data_channel.channel_cap.slot_cfg_id,
                             p_gh_data[i].cap_data.rawdata);
            break;

        default:
            break;
        }//switch(data_type)

    }//for (uint16_t i = 0; i < len; i++)
#endif

#if (1 == GH_USE_SDK_APP)
    /* Get rawadata or debug data from here */
    for (uint16_t i = 0; i < len; i++)
    {
        /* If the customer needs fusion module, call gh_demo_ghealth_data_set and get data from gh_demo_data_publish */
        gh_demo_ghealth_data_set(p_gh_data + i);
    }
#endif
}
