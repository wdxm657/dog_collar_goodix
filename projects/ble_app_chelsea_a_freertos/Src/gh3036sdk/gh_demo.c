
#include "gh_demo.h"
#include "gh_reg_lists.h"
#include "gh_hal_efuse_read.h"
#include "app_thread.h"
#include "user_app.h"
#include "factory.h"
#include "gh_gsensor_bridge.h"
#include <inttypes.h>
#if GH_USER_LOG_EN
#define DEBUG_LOG(...)                      GH_LOG_LVL_DEBUG(__VA_ARGS__)
#define WARNING_LOG(...)                    GH_LOG_LVL_WARNING(__VA_ARGS__)
#define ERROR_LOG(...)                      GH_LOG_LVL_ERROR(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#define WARNING_LOG(...)
#define ERROR_LOG(...)
#endif

static gh_function_en_union_t s_func_en; 
static uint8_t current_cfg_index = 255;
static uint8_t s_init_flag = 0;

uint64_t efuse_256bit[4];

extern int8_t *gh_sdk_version_get(void);

extern void gh_hal_set_interface_type(uint8_t interface_type);
extern uint32_t gh_hal_spi_deinit(void);
extern uint32_t gh_hal_spi_init(void);

extern uint32_t gh_hal_i2c_deinit(void);
extern uint32_t gh_hal_i2c_init(void);

uint32_t gh_hal_i2c_switch_to_spi(void)
{
    uint32_t ret = 0;
    gh_hal_i2c_deinit();
    gh_hal_set_interface_type(GH_INTERFACE_SPI_SW_CS);
    gh_hal_spi_init();
    ret = GH_RET_HAL_ERR_GET(gh_hal_service_init());
    if (GH_SERVICE_OK == ret)
    {
      DEBUG_LOG("[SPI]gh hal service init ok!\r\n");  
    }
    else
    {
        ERROR_LOG("[SPI]gh hal service init fail!\r\n");
    }
    return ret;
}

uint32_t gh_hal_spi_switch_to_i2c(void)
{
    uint32_t ret = 0;
    gh_hal_spi_deinit();
    gh_hal_set_interface_type(GH_INTERFACE_I2C);
    gh_hal_i2c_init();
    ret = GH_RET_HAL_ERR_GET(gh_hal_service_init());
    if (GH_SERVICE_OK == ret)
    {
      DEBUG_LOG("[I2C]gh hal service init ok!\r\n");  
    }
    else
    {
        ERROR_LOG("[I2C]gh hal service init fail!\r\n");
    }
    return ret;
}

void gh_app_demo_init(void)
{
    int ret = GH_API_OK;
    if(GH_API_OK == gh_demo_init())
    {
        DEBUG_LOG("gh demo init ok!\r\n");
    }
    else
    {
        ERROR_LOG("gh demo init fail!\r\n");
        ret = GH_API_INIT_FAIL_ALREADY_INIT;
    }
    ret = gh_hal_i2c_switch_to_spi();
    if(GH_SERVICE_OK != ret)
    {
        ret = gh_hal_spi_switch_to_i2c();
    }
    
    gh_hal_mutex_init();
    gh_gsensor_bridge_init();
#if (1 == GH_PROTOCOL_EN)
    gh_protocol_init();
#endif
    DEBUG_LOG("SDK version: %s\r\n", gh_sdk_version_get());
    
    
    if (GH_API_OK == ret)
    {
        gh_hal_mutex_lock();
        gh_efuse_read_all(efuse_256bit);
        gh_hal_mutex_unlock();
        DEBUG_LOG("Efuse: %016" PRIX64 "-%016" PRIX64 "-%016" PRIX64 "-%016" PRIX64 "\r\n",
            efuse_256bit[0], efuse_256bit[1],
            efuse_256bit[2], efuse_256bit[3]
            );
        
        
        s_init_flag = 1;
        gh_hal_delay_ms(10);
        gh_app_demo_cfg_switch(0);
        gh_hal_delay_ms(10); 
        DEBUG_LOG("gh demo config download ok!\r\n");
        s_func_en.bits = 0;
        gh_hal_mutex_lock();
        gh_demo_function_get(&s_func_en);
        gh_hal_mutex_unlock();
        DEBUG_LOG("s_func_en.bits = %X!\r\n", s_func_en.bits);
        s_func_en.bits = 0;
    }
    else 
    {
        s_init_flag = 0;
    }
    
}

void gh_app_demo_deinit(void)
{
    if(GH_API_OK == gh_demo_deinit())
    {
        s_init_flag = 0;
        DEBUG_LOG("gh demo deinit ok!\r\n");
    }
    else
    {
        ERROR_LOG("gh demo deinit fail!\r\n");
    }
}

void gh_app_demo_start(uint32_t mode)
{
    if (0 == s_init_flag) {
        ERROR_LOG("[%s]gh sdk no init!\r\n", __FUNCTION__);
        return;
    }
    s_func_en.bits |= mode;
    gh_assist_en_union_t assist;
    assist.bits = 0;
    assist.assist.assist_gsensor_en = 1;
    gh_demo_assist_config(&s_func_en, &assist);
    gh_hal_mutex_lock();
    gh_demo_function_ctrl(&s_func_en);
    gh_hal_mutex_unlock();
}

void gh_app_demo_stop(uint32_t mode)
{
    if (0 == s_init_flag) {
        ERROR_LOG("[%s]gh sdk no init!\r\n", __FUNCTION__);
        return;
    }
    s_func_en.bits &= ~mode;
    gh_hal_mutex_lock();
    gh_demo_function_ctrl(&s_func_en);
    gh_hal_mutex_unlock();
}

void gh_app_demo_cfg_switch(uint8_t index)
{
    if (0 == s_init_flag) {
        ERROR_LOG("[%s]gh sdk no init!\r\n", __FUNCTION__);
        return;
    }
	if ((index != current_cfg_index) && (index < g_reg_lists_max_size)) {
        current_cfg_index = index;
        gh_app_demo_stop(s_func_en.bits);
        gh_hal_mutex_lock();
        gh_demo_config_write ((gh_config_reg_t *) g_reg_lists[index].reg_cfg, g_reg_lists[index].reg_cfg_len);
        gh_hal_mutex_unlock();
    }
}
uint8_t active_move_flag = 0;
void gh_app_demo_int_process(void)
{
    if (0 == s_init_flag) {
        ERROR_LOG("[%s]gh sdk no init!\r\n", __FUNCTION__);
        return;
    }
    gh_hal_mutex_lock();
    gh_hal_isr();
    gh_hal_mutex_unlock();
}

void gh_app_demo_read_efuse(uint64_t efuse_256bit[4])
{
    if (0 == s_init_flag)
    {
        efuse_256bit[0] = 0xFFFFFFFF;
        efuse_256bit[1] = 0xFFFFFFFF;
        efuse_256bit[2] = 0xFFFFFFFF;
        efuse_256bit[3] = 0xFFFFFFFF;
        return;
    }
    gh_hal_mutex_lock();
    gh_efuse_read_all(efuse_256bit);
    gh_hal_mutex_unlock();
}


void health_register_api(void) {
    health_register_api_ctx(
        gh_app_demo_init,
        gh_app_demo_start,
        gh_app_demo_stop,
        gh_app_demo_int_process,
        gh_app_demo_cfg_switch
        );
}

/**
 * @brief set work mode
 * @param uchWorkMode work mode
 *                  0 MCU在线模式 (MCU online mode)
 *                  1 MCU离线模式 (MCU offline mode)
 *                  2 量产测试模式 (MPT test mode)
 * @return void
 **/
void GHSetWorkModeCmd(uint8_t uchWorkMode)
{
    DEBUG_LOG("GHSetWorkModeCmd = %d", uchWorkMode);
    health_work_mode_update_event_send(uchWorkMode);
    if (uchWorkMode == 2)
    {
        app_set_rtc_tick_period(200);
        factory_init();
    } else {
        app_set_rtc_tick_period(1000);
        factory_deinit();
    }
}


uint8_t factory_init_status(void)
{
    return s_init_flag;
}

void factory_chip_uid_get(uint16_t *p_chip_uid, uint8_t len)
{
    if (s_init_flag) {
        memcpy((uint8_t*)p_chip_uid, (uint8_t*)efuse_256bit, len * 2);
    }
}
