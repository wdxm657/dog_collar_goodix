#ifndef __LIS2DW12_H__
#define __LIS2DW12_H__

#include "lis2dw12_reg.h"

#ifdef  __ZEPHYR__
#include <zephyr/kernel.h>
typedef struct k_timer gsensor_timer;
typedef struct k_timer* gsensor_timer_t;
#else
#include "FreeRTOS.h"
#include "timers.h"

typedef TimerHandle_t gsensor_timer_t;
#endif

#include "app_i2c.h"
#include "app_i2c_dma.h"
#include "app_io.h"
#include "app_gpiote.h"
#include "app_thread_imu.h"


#define LIS2DW12_INT_PIN        APP_IO_PIN_2
#define LIS2DW12_INT_TYPE       APP_IO_TYPE_AON
#define LIS2DW12_INT_IT_MODE    APP_IO_MODE_IT_RISING
#define LIS2DW12_INT_TIMER_PERIOD_MS  (100)

#define LIS2DW12_SLAVE_ADDR     0x19
#define LIS2DW12_MASTER_ADDR    0x38

#define LIS2DW12_I2C_ID         APP_I2C_ID_5
#define LIS2DW12_I2C_USE_DMA    (0)

#define LIS2DW12_SCL_PIN        APP_IO_PIN_8
#define LIS2DW12_SCL_TYPE       APP_IO_TYPE_GPIOA
#define LIS2DW12_SCL_MUX        APP_IO_MUX_0

#define LIS2DW12_SDA_PIN        APP_IO_PIN_9
#define LIS2DW12_SDA_TYPE       APP_IO_TYPE_GPIOA
#define LIS2DW12_SDA_MUX        APP_IO_MUX_0

#define LIS2DW12_DEFAULT_I2C_IO_CONFIG         { { LIS2DW12_SCL_TYPE, LIS2DW12_SCL_MUX, LIS2DW12_SCL_PIN, APP_IO_PULLUP }, \
                                                 { LIS2DW12_SDA_TYPE, LIS2DW12_SDA_MUX, LIS2DW12_SDA_PIN, APP_IO_PULLUP } }
#define LIS2DW12_DEFAULT_I2C_MODE_CONFIG       { DMA0, DMA0, DMA_Channel2, DMA_Channel3 }
#define LIS2DW12_DEFAULT_I2C_CONFIG            { I2C_SPEED_400K, LIS2DW12_MASTER_ADDR, I2C_ADDRESSINGMODE_7BIT, I2C_GENERALCALL_ENABLE}
#define LIS2DW12_DEFAULT_I2C_PARAM_CONFIG      { LIS2DW12_I2C_ID, APP_I2C_ROLE_MASTER, LIS2DW12_DEFAULT_I2C_IO_CONFIG, LIS2DW12_DEFAULT_I2C_MODE_CONFIG, LIS2DW12_DEFAULT_I2C_CONFIG}

typedef void (*lis2dw12_event_cb_t)(lis2dw12_all_sources_t val);

typedef struct
{
  uint8_t addr;
  app_i2c_params_t *i2c_param;
  stmdev_ctx_t *ctx;
  lis2dw12_event_cb_t event_cb;
  lis2dw12_odr_t odr;
  bool is_init;
} lis2dw12_info_t;

typedef struct
{
    int16_t acc_x;
    int16_t acc_y;
    int16_t acc_z;
}__attribute__((packed)) lis2dw12_acc_data_t;




extern lis2dw12_info_t lis2dw12_info;

void app_lis2dw12_init(lis2dw12_info_t *info, uint8_t addr, lis2dw12_event_cb_t event_cb);
int32_t app_lis2dw12_read_reg(lis2dw12_info_t *info, uint8_t reg, uint8_t *data, uint16_t len);
int32_t app_lis2dw12_write_reg(lis2dw12_info_t *info, uint8_t reg, uint8_t *data, uint16_t len);

int32_t lis2dw12_acceleration_raws_get(const stmdev_ctx_t *ctx,
                                      int16_t *val, uint8_t len);

#define APP_LIS2DW12_DEFAULT_INIT(event_cb)            app_lis2dw12_init(&lis2dw12_info, LIS2DW12_SLAVE_ADDR, event_cb)
#define APP_LIS2DW12_DEFAULT_READ_REG(reg, data, len)  app_lis2dw12_read_reg(&lis2dw12_info, reg, data, len)
#define APP_LIS2DW12_DEFAULT_WRITE_REG(reg, data, len) app_lis2dw12_write_reg(&lis2dw12_info, reg, data, len)
#define APP_LIS2DW12_DEFAULT_CTX()                     (lis2dw12_info.ctx)


#endif
