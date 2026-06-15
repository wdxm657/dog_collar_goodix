
#include "lis2dw12.h"
#include "app_i2c.h"
#include "app_i2c_dma.h"
#include "app_thread.h"

static stmdev_ctx_t lis2dw12_ctx = {
  .read_reg = (stmdev_read_ptr)app_lis2dw12_read_reg,
  .write_reg = (stmdev_write_ptr)app_lis2dw12_write_reg,
  .mdelay = delay_ms,
};

static app_i2c_params_t lis2dw12_i2c_param = LIS2DW12_DEFAULT_I2C_PARAM_CONFIG;

lis2dw12_info_t lis2dw12_info = {
    .addr = LIS2DW12_SLAVE_ADDR,
    .i2c_param = &lis2dw12_i2c_param,
    .ctx = NULL,
    .odr = LIS2DW12_XL_ODR_25Hz,
    .is_init = false
};

static void app_lis2dw12_int_init(void);
static void app_lis2dw12_default_config(void);
static void app_lis2dw12_default_config_log(void);


static void app_lis2dw12_event_cb(app_io_evt_t *p_evt);


void app_lis2dw12_init(lis2dw12_info_t *info, uint8_t addr, lis2dw12_event_cb_t event_cb)
{
    int re = 0;
    if (info == NULL)
    {
        printf("lis2dw12_init failed, info is NULL\r\n");
        return;
    }
    if (addr != 0x00)
    {
        lis2dw12_info.addr = addr;
    }
    else
    {
        lis2dw12_info.addr = LIS2DW12_SLAVE_ADDR;
    }
    if (event_cb != NULL)
    {
        info->event_cb = event_cb;
    }
    else
    {
        info->event_cb = NULL;
    }
    lis2dw12_ctx.handle = info;
    info->i2c_param = &lis2dw12_i2c_param;
    re = app_i2c_init(info->i2c_param, NULL);
    if(re != 0)
    {
        printf("app_i2c_init failed, re = %d\r\n", re);
    }
#if LIS2DW12_I2C_USE_DMA
    re = app_i2c_dma_init(info->i2c_param);
    if(re != 0)
    {
        printf("app_i2c_dma_init failed, re = %d\r\n", re);
    }
#endif
    uint8_t lis2dw12_device_id = 0x00;
    lis2dw12_device_id_get(&lis2dw12_ctx, &lis2dw12_device_id);
    if (lis2dw12_device_id != LIS2DW12_ID){
        printf("lis2dw12 failed, lis2dw12_device_id = %d\r\n", lis2dw12_device_id);
        return;
    }
    lis2dw12_info.ctx = &lis2dw12_ctx;
    lis2dw12_info.is_init = true;
    app_lis2dw12_int_init();
    app_lis2dw12_default_config();
    app_lis2dw12_default_config_log();

}


int32_t app_lis2dw12_read_reg(lis2dw12_info_t *info, uint8_t reg, uint8_t *data, uint16_t len)
{
  return app_i2c_mem_read_sync(info->i2c_param->id, info->addr, reg, 8, data, len, 1000);
}

int32_t app_lis2dw12_write_reg(lis2dw12_info_t *info, uint8_t reg, uint8_t *data, uint16_t len)

{
  return app_i2c_mem_write_sync(info->i2c_param->id, info->addr, reg, 8, data, len, 1000);
}


static void app_lis2dw12_event_cb(app_io_evt_t *p_evt)
{
    if (p_evt->pin == LIS2DW12_INT_PIN)
    {
        lis2dw12_all_sources_t all_sources;
        lis2dw12_all_sources_get(lis2dw12_info.ctx, &all_sources);
        if(lis2dw12_info.event_cb != NULL)
        {
            if (*(uint8_t *)&all_sources.status_dup != 0) {
                lis2dw12_info.event_cb(all_sources);
            }
        }
    }
}



static void app_lis2dw12_int_init(void)
{
    app_gpiote_param_t lis2dw12_int_param = {
        LIS2DW12_INT_TYPE, LIS2DW12_INT_PIN, LIS2DW12_INT_IT_MODE, APP_IO_PULLUP, app_lis2dw12_event_cb
    };
    app_gpiote_init(&lis2dw12_int_param, 1);
}



static void app_lis2dw12_default_config(void)
{
    lis2dw12_power_mode_set(lis2dw12_info.ctx, LIS2DW12_CONT_LOW_PWR_4);
    lis2dw12_data_rate_set(lis2dw12_info.ctx, LIS2DW12_XL_ODR_25Hz);
    lis2dw12_auto_increment_set(lis2dw12_info.ctx, 1);
    lis2dw12_full_scale_set(lis2dw12_info.ctx, LIS2DW12_4g);
    lis2dw12_fifo_watermark_set(lis2dw12_info.ctx, 25);
    lis2dw12_fifo_mode_set(lis2dw12_info.ctx, LIS2DW12_STREAM_TO_FIFO_MODE);

    lis2dw12_reg_t int_route;
    lis2dw12_pin_int1_route_get(lis2dw12_info.ctx,
					&int_route.ctrl4_int1_pad_ctrl);
    int_route.ctrl4_int1_pad_ctrl.int1_fth = 1;
    lis2dw12_pin_int1_route_set(lis2dw12_info.ctx,
					&int_route.ctrl4_int1_pad_ctrl);
    
    lis2dw12_lir_t lir_val;
    lis2dw12_int_notification_get(lis2dw12_info.ctx, &lir_val);
    printf("lir_val = 0x%x\r\n", lir_val);
    lis2dw12_int_notification_set(lis2dw12_info.ctx, LIS2DW12_INT_PULSED);
//    lis2dw12_reg_t int_route;
//    lis2dw12_pin_int2_route_get(lis2dw12_info.ctx,
//					&int_route.ctrl5_int2_pad_ctrl);
//    int_route.ctrl5_int2_pad_ctrl.int2_fth = 1;
//    lis2dw12_pin_int2_route_set(lis2dw12_info.ctx,
//					&int_route.ctrl5_int2_pad_ctrl);  

}

static void app_lis2dw12_default_config_log(void)
{
    printf("app_lis2dw12_default_config_log\r\n");
    printf("LIS2DW12_SLAVE_ADDR = 0x%x\r\n", LIS2DW12_SLAVE_ADDR);

    lis2dw12_mode_t power_mode;
    lis2dw12_power_mode_get(lis2dw12_info.ctx, &power_mode);
    printf("power_mode = 0x%x\r\n", power_mode);

    lis2dw12_odr_t data_rate;
    lis2dw12_data_rate_get(lis2dw12_info.ctx, &data_rate);
    printf("data_rate = 0x%x\r\n", data_rate);
    
    uint8_t auto_increment;
    lis2dw12_auto_increment_get(lis2dw12_info.ctx, &auto_increment);
    printf("auto_increment = 0x%x\r\n", auto_increment);

    lis2dw12_fs_t full_scale;
    lis2dw12_full_scale_get(lis2dw12_info.ctx, &full_scale);
    printf("full_scale = 0x%x\r\n", full_scale);

    uint8_t fifo_watermark;
    lis2dw12_fifo_watermark_get(lis2dw12_info.ctx, &fifo_watermark);
    printf("fifo_watermark = 0x%x\r\n", fifo_watermark);

    lis2dw12_fmode_t fifo_mode;
    lis2dw12_fifo_mode_get(lis2dw12_info.ctx, &fifo_mode);
    printf("fifo_mode = 0x%x\r\n", fifo_mode);


    
}




int32_t lis2dw12_acceleration_raws_get(const stmdev_ctx_t *ctx,
                                      int16_t *val, uint8_t len)
{
  int32_t ret;
  uint8_t is_auto_increment = 0;
//  lis2dw12_all_sources_t   all_sources;
    
  if (!lis2dw12_info.is_init){
      return -1;
  }

  lis2dw12_auto_increment_get(ctx, &is_auto_increment);
  if (is_auto_increment) {
        ret = lis2dw12_read_reg(ctx, LIS2DW12_OUT_X_L, (uint8_t*)val, 6 * len);
  }
  else {
        for (uint8_t i = 0; i < len; i++) {
            ret = lis2dw12_acceleration_raw_get(ctx, val + i * 6);
        }
  }
  return ret;
}

void lis2dw12_init_api(void) {
    app_lis2dw12_init(&lis2dw12_info, LIS2DW12_SLAVE_ADDR, NULL);
}

void lis2dw12_start_api(void) {
    lis2dw12_data_rate_set(lis2dw12_info.ctx, lis2dw12_info.odr);
}

void lis2dw12_stop_api(void) {
    lis2dw12_data_rate_set(lis2dw12_info.ctx, LIS2DW12_XL_ODR_OFF);
}

static uint32_t lis2dw12_get_sample_interval_ms(void)
{
    switch (lis2dw12_info.odr)
    {
    case LIS2DW12_XL_ODR_1Hz6_LP_ONLY:
        return 625;
    case LIS2DW12_XL_ODR_12Hz5:
        return 80;
    case LIS2DW12_XL_ODR_25Hz:
        return 40;
    case LIS2DW12_XL_ODR_50Hz:
        return 20;
    case LIS2DW12_XL_ODR_100Hz:
        return 10;
    case LIS2DW12_XL_ODR_200Hz:
        return 5;
    case LIS2DW12_XL_ODR_400Hz:
        return 3;
    case LIS2DW12_XL_ODR_800Hz:
        return 2;
    case LIS2DW12_XL_ODR_1k6Hz:
        return 1;
    default:
        return 40;
    }
}

int32_t lis2dw12_read_data_api(imu_data_t* imu_data, uint8_t* len, uint64_t current_timestamp) {
    if ((imu_data == NULL) || (len == NULL) || (*len == 0)) {
        return -1;
    }
    uint8_t fifo_level = 0;
    uint8_t read_len = 0;
    uint32_t sample_interval_ms = lis2dw12_get_sample_interval_ms();
    lis2dw12_fifo_data_level_get(lis2dw12_info.ctx, &fifo_level);
    if (fifo_level > 0) {
        read_len = (fifo_level > *len) ? *len : fifo_level;
        int16_t* acc_buf = osal_heap_malloc(read_len * 3 * sizeof(int16_t));
        if (acc_buf != NULL) {
            lis2dw12_acceleration_raws_get(lis2dw12_info.ctx, acc_buf, read_len);
            for (uint8_t i = 0; i < read_len; i ++){
                imu_data[i].timestamp = current_timestamp - (uint64_t)(read_len - i - 1) * sample_interval_ms;
                imu_data[i].accel_x   = acc_buf[i * 3 + 0];
                imu_data[i].accel_y   = acc_buf[i * 3 + 1];
                imu_data[i].accel_z   = acc_buf[i * 3 + 2];
                imu_data[i].gyro_x    = 0;
                imu_data[i].gyro_y    = 0;
                imu_data[i].gyro_z    = 0;
            }
            *len = read_len;
            osal_heap_free(acc_buf);
        } else {
            *len = 0;
            return -2;
        }
    } else {
        *len = 0;
    }
    return 0;
}

void lis2dw12_set_fs_api(uint32_t fs) {
    lis2dw12_full_scale_set(lis2dw12_info.ctx, (lis2dw12_fs_t)fs);
}

void lis2dw12_set_dl_api(uint32_t dl) {
    lis2dw12_power_mode_set(lis2dw12_info.ctx, (lis2dw12_mode_t)dl);
}

void lis2dw12_set_odr_api(uint32_t odr){
    lis2dw12_data_rate_set(lis2dw12_info.ctx, (lis2dw12_odr_t)odr);
    lis2dw12_info.odr = (lis2dw12_odr_t)odr;
}

void lis2dw12_set_fifo_api(uint32_t fifo_watermark){
    lis2dw12_fifo_watermark_set(lis2dw12_info.ctx, fifo_watermark);
    lis2dw12_fifo_mode_set(lis2dw12_info.ctx, LIS2DW12_STREAM_TO_FIFO_MODE);
}

void imu_register_api(void)
{
    printf("imu_register_api\r\n");
    imu_register_api_ctx(
        lis2dw12_init_api,
        lis2dw12_start_api,
        lis2dw12_stop_api,
        lis2dw12_read_data_api,
        lis2dw12_set_fs_api,
        lis2dw12_set_dl_api,
        lis2dw12_set_odr_api,
        lis2dw12_set_fifo_api
        );
}

