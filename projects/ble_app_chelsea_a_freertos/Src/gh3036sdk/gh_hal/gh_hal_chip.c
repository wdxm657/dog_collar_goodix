#include <stdint.h>
#include "gh_hal_interface.h"
#include "gh_hal_chip.h"
#include "gh_hal_utils.h"
#include "gh_hal_user.h"
#include "gh_hal_config.h"
#include "gh3036_reg.h"

uint32_t gh3036_top_start(uint8_t start)
{
    return gh_hal_reg_write(RG_TOP_START_ADDR, start);
}

uint32_t gh3036_set_slot_en(gh3036_slot_en_t* p_slot_en)
{
    uint16_t cap_cancel_en;
    uint32_t ret = gh_hal_reg_bit_field_read(RG_CAP_CANCEL_EN_ADDR, RG_CAP_CANCEL_EN_LSB,
                                             RG_CAP_CANCEL_EN_MSB, &cap_cancel_en);
    if (0 == cap_cancel_en)
    {
        ret |= gh_hal_reg_write(RG_SLOT_EN_ADDR, p_slot_en->slot_en);
    }
    return ret;
}

uint32_t gh3036_set_fifo_watermark(uint16_t fifo_watermark)
{
    uint16_t ret = gh_hal_reg_bit_field_write(RG_FIFO_WATERLINE_ADDR,
                                              RG_FIFO_WATERLINE_LSB,
                                              RG_FIFO_WATERLINE_MSB,
                                              fifo_watermark);
    return ret;
}

uint32_t gh_get_fifo_use(uint16_t* p_fifo_use)
{
    uint32_t ret = gh_hal_reg_bit_field_read(RG_FIFO_O_USED_ADDR, RG_FIFO_O_USED_LSB,
                                             RG_FIFO_O_USED_MSB, p_fifo_use);
    return 0;
}

uint32_t gh_get_fifo_watermark(uint16_t* p_fifo_watermark)
{
    uint32_t ret = gh_hal_reg_bit_field_read(RG_FIFO_WATERLINE_ADDR, RG_FIFO_WATERLINE_LSB,
                                             RG_FIFO_WATERLINE_MSB, p_fifo_watermark);
    return 0;
}

uint32_t gh_get_irq(gh_hal_isr_status_t* p_isr_status)
{
    uint32_t ret = gh_hal_reg_bit_field_read(INT_STATUS_ADDR, INT_STATUS_LSB, INT_STATUS_MSB, (uint16_t *)p_isr_status);
    if (*((uint16_t *)p_isr_status) != 0)
    {
        ret |= gh_hal_reg_write(RG_INT_CLR_ADDR, *((uint16_t *)p_isr_status));
    }
    return ret;
}

uint32_t gh_fifo_read(uint8_t* p_buffer, uint16_t* p_buffer_idx,
                      uint16_t buffer_size, uint32_t* p_fifo_use_byte_len,
                      uint8_t* p_continue)
{
    uint32_t ret = 0;

    if (GH_NULL_PTR == p_buffer || GH_NULL_PTR == p_buffer_idx || 0 == buffer_size
        || GH_NULL_PTR == p_fifo_use_byte_len || GH_NULL_PTR == p_continue)
    {
        ret = GH_HAL_PTR_NULL;
        return ret;
    }

    *p_continue = ((uint32_t)buffer_size < (*p_fifo_use_byte_len)) ? 1 : 0;
    *p_buffer_idx = ((uint32_t)buffer_size < (*p_fifo_use_byte_len)) ? buffer_size : (*p_fifo_use_byte_len);
    *p_fifo_use_byte_len -= *p_buffer_idx;
    ret |= gh_hal_fifo_read(p_buffer, *p_buffer_idx);

    return ret;
}

uint32_t gh3036_set_slot_index(uint8_t* p_slot_index, uint16_t len)
{
    uint16_t slot_idx_reg[GH3036_SLOT_INDEX_REG_NUM];
    uint8_t reg_idx;
    uint8_t reg_offset;
    uint8_t reg_field_width = RG_SLOT_INDEX0_MSB - RG_SLOT_INDEX0_LSB + 1;
    uint8_t reg_field_mask = (1 << reg_field_width) - 1;

    gh_memset(slot_idx_reg, GH3036_SLOT_INDEX_DEFAULT_VAL, GH3036_SLOT_INDEX_REG_NUM * sizeof(slot_idx_reg[0]));
    for (uint8_t i = 0; i < len; ++i)
    {
        reg_idx = (uint8_t)i / reg_field_width;
        reg_offset = i % reg_field_width;
        slot_idx_reg[reg_idx] &= ~(reg_field_mask << (reg_field_width * reg_offset));
        slot_idx_reg[reg_idx] |=  ((p_slot_index[i] & reg_field_mask) << (reg_offset * reg_field_width));
    }
    gh_hal_regs_write(RG_SLOT_INDEX0_ADDR, slot_idx_reg, GH3036_SLOT_INDEX_REG_NUM);
    return 0;
}

uint32_t gh3036_std_parse_config(uint16_t reg_addr, uint16_t reg_val, uint8_t slot_cfg_num,
                                 uint8_t* dc_cancel_range_0,
                                 uint8_t* dc_cancel_range_1,
                                 uint8_t* dc_cancel_code_0,
                                 uint8_t* dc_cancel_code_1)
{
    uint32_t ret = 0;

    if (reg_addr == (uint16_t)(RG_RX0_DC_CANCEL_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_DC_CANCEL_CODE_0_LSB, RG_RX0_DC_CANCEL_CODE_0_MSB,
                             (uint16_t*)dc_cancel_code_0);
        gh_hal_get_reg_field(reg_val, RG_RX1_DC_CANCEL_CODE_0_LSB, RG_RX1_DC_CANCEL_CODE_0_MSB,
                             (uint16_t*)dc_cancel_code_1);
    }
    else if (reg_addr == (uint16_t)(RG_RX0_DC_CANCEL_RANGE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_DC_CANCEL_RANGE_0_LSB, RG_RX0_DC_CANCEL_RANGE_0_MSB,
                             (uint16_t*)dc_cancel_range_0);
        gh_hal_get_reg_field(reg_val, RG_RX1_DC_CANCEL_RANGE_0_LSB, RG_RX1_DC_CANCEL_RANGE_0_MSB,
                             (uint16_t*)dc_cancel_range_1);
    }
    return ret;
}

uint32_t gh3036_set_led_code(uint8_t code, uint8_t drv_num, uint8_t slot_cfg_num)
{
    if (0 == drv_num)
    {
        gh_hal_reg_bit_field_write(RG_LED_DRIVER0_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num,
                                   RG_LED_DRIVER0_CODE_0_LSB, RG_LED_DRIVER0_CODE_0_MSB, code);
    }
    else
    {
        gh_hal_reg_bit_field_write(RG_LED_DRIVER1_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num,
                                   RG_LED_DRIVER1_CODE_0_LSB, RG_LED_DRIVER1_CODE_0_MSB, code);
    }

    return 0;
}

uint32_t gh3036_set_dc_cancel_code(uint8_t code, uint8_t rx, uint8_t slot_cfg_num)
{
    if (0 == rx)
    {
        gh_hal_reg_bit_field_write(RG_RX0_DC_CANCEL_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num,
                                   RG_RX0_DC_CANCEL_CODE_0_LSB, RG_RX0_DC_CANCEL_CODE_0_MSB, code);
    }
    else
    {
        gh_hal_reg_bit_field_write(RG_RX1_DC_CANCEL_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num,
                                   RG_RX1_DC_CANCEL_CODE_0_LSB, RG_RX1_DC_CANCEL_CODE_0_MSB, code);
    }

    return 0;
}

#if GH_HAL_STD_CALI_EN
/**
 * @fn     uint32_t gh3036_efuse_control(uint8_t control);
 *
 * @brief  Start/stop efuse.
 *
 * @attention   None
 *
 * @param[in]   control              efuse start/stop control
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh3036_efuse_control(uint8_t control)
{
    if (control <= 1)
    {
        gh_hal_reg_bit_field_write(RG_EFUSE_REG_MODE_ADDR, RG_EFUSE_REG_MODE_LSB, RG_EFUSE_REG_MODE_MSB, control);
        gh_hal_reg_bit_field_write(RG_EFUSE_RDEN_ADDR, RG_EFUSE_RDEN_LSB, RG_EFUSE_RDEN_MSB, control);
    }
    return 0;
}

/**
 * @fn     gh3036_efuse_value_get(uint8_t addr, uint8_t* p_value);
 *
 * @brief  Get efuse value.
 *
 * @attention   None
 *
 * @param[in]   addr              efuse addr
 * @param[in]   p_value           pointer of value
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh3036_efuse_value_get(uint16_t addr, uint16_t* p_value)
{
    uint32_t ret = 0;
    gh_hal_reg_bit_field_write(RG_EFUSE_ADDR_ADDR, RG_EFUSE_ADDR_LSB, RG_EFUSE_ADDR_MSB, addr);
    gh_hal_reg_bit_field_write(RG_EFUSE_AEN_ADDR, RG_EFUSE_AEN_LSB, RG_EFUSE_AEN_MSB, 1);
    gh_hal_reg_bit_field_write(RG_EFUSE_AEN_ADDR, RG_EFUSE_AEN_LSB, RG_EFUSE_AEN_MSB, 0);
    gh_hal_reg_bit_field_read(RG_EFUSE_D_ADDR, RG_EFUSE_D_LSB, RG_EFUSE_D_MSB, p_value);
    return ret;
}

uint32_t gh_hal_efuse_cali_param_get(gh_hal_std_cali_param_t* p_cali_param)
{
    uint32_t ret = 0;
    uint16_t reg_val;

    gh3036_efuse_control(RG_EFUSE_START);

#if GH_HAL_STD_CALI_DRV_EN
    //LED cali param
    gh3036_efuse_value_get(RG_EFUSE_LED0_ER_ADDR, &reg_val);
    p_cali_param->led_param.led0_er = reg_val;
    gh3036_efuse_value_get(RG_EFUSE_LED1_ER_ADDR, &reg_val);
    p_cali_param->led_param.led1_er = reg_val;
#endif

#if GH_HAL_STD_CALI_DC_CANCEL_EN
    //dc cancel cali param
    gh3036_efuse_value_get(RG_EFUSE_DC_CANCEL_ER1_ADDR, &reg_val);
    p_cali_param->dc_cancel_param.dc_cancel_a = reg_val;
    gh3036_efuse_value_get(RG_EFUSE_DC_CANCEL_ER2_ADDR, &reg_val);
    p_cali_param->dc_cancel_param.dc_cancel_b = reg_val;
#endif

#if GH_HAL_STD_CALI_BG_CANCEL_EN
    //bg cancel cali param
    gh3036_efuse_value_get(RG_EFUSE_BG_CANCEL_ER1_ADDR, &reg_val);
    p_cali_param->bg_cancel_param.bg_cancel_a = reg_val;
    gh3036_efuse_value_get(RG_EFUSE_BG_CANCEL_ER2_ADDR, &reg_val);
    p_cali_param->bg_cancel_param.bg_cancel_b = reg_val;
#endif

#if GH_HAL_STD_CALI_RX_OFFSET_EN
    //rx offset cali param
    gh3036_efuse_value_get(RG_EFUSE_RX0_OFFSET_ER_ADDR, &reg_val);
    p_cali_param->rx_param.rx0_offset = reg_val;
    gh3036_efuse_value_get(RG_EFUSE_RX1_OFFSET_ER_ADDR, &reg_val);
    p_cali_param->rx_param.rx1_offset = reg_val;
#endif

#if GH_HAL_STD_CALI_GAIN_EN
    //rx gain cali param
    gh3036_efuse_value_get(RG_EFUSE_RX0_GAIN_ER_ADDR, &reg_val);
    p_cali_param->gain_param.gain_rx0_offset = reg_val;
    gh3036_efuse_value_get(RG_EFUSE_RX1_GAIN_ER_ADDR, &reg_val);
    p_cali_param->gain_param.gain_rx1_offset = reg_val;
#endif

    gh3036_efuse_control(RG_EFUSE_STOP);

    return ret;
}
#endif

uint32_t gh3036_global_cfg_parse(uint16_t reg_addr, uint16_t reg_val,
                                 gh3036_global_cfg_param_t* p_gh3036_global_cfg_param)
{
    uint32_t ret = 0;
    uint16_t val = 0;
    uint8_t led_drv_fs_code;

    if (RG_LEDDRV_FS_CH0_ADDR == reg_addr)
    {
        gh_hal_get_reg_field(reg_val, RG_LEDDRV_FS_CH0_LSB, RG_LEDDRV_FS_CH0_MSB, &val);
        p_gh3036_global_cfg_param->led_drv_fs[0] = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_LEDDRV_FS_CH1_LSB, RG_LEDDRV_FS_CH1_MSB, &val);
        p_gh3036_global_cfg_param->led_drv_fs[1] = (uint8_t)val;

        led_drv_fs_code = p_gh3036_global_cfg_param->led_drv_fs[0];
        p_gh3036_global_cfg_param->led_drv_fs[0] = LED_DRV_FS_BASE;
        for (uint8_t i = 0; i < led_drv_fs_code; i++)
        {
            p_gh3036_global_cfg_param->led_drv_fs[0] *= LED_DRV_FS_MULTI;
        }

        led_drv_fs_code = p_gh3036_global_cfg_param->led_drv_fs[1];
        p_gh3036_global_cfg_param->led_drv_fs[1] = LED_DRV_FS_BASE;
        for (uint8_t i = 0; i < led_drv_fs_code; i++)
        {
            p_gh3036_global_cfg_param->led_drv_fs[1] *= LED_DRV_FS_MULTI;
        }
    }
    if (RG_CAP_SR_MULTIPLIER_ADDR == reg_addr)
    {
        gh_hal_get_reg_field(reg_val, RG_CAP_SR_MULTIPLIER_LSB, RG_CAP_SR_MULTIPLIER_MSB, &val);
        p_gh3036_global_cfg_param->cap_cfg_multiplier = val;
    }
    if ((RG_FASTEST_SAMPLE_RATE_ADDR) == reg_addr)
    {
        gh_hal_get_reg_field(reg_val, RG_FASTEST_SAMPLE_RATE_LSB, RG_FASTEST_SAMPLE_RATE_MSB, &val);
        p_gh3036_global_cfg_param->fastest_sample_rate = val;
    }
    return ret;
}

uint32_t gh3036_ppg_slot_config(uint16_t reg_addr, uint16_t reg_val, uint8_t slot_cfg_num,
                                gh3036_ppg_cfg_param_t* p_gh3036_ppg_cfg_param)
{
    uint32_t ret = 0;
    uint16_t val = 0;
    if (reg_addr == (uint16_t)((RG_RX0_TIA_GAIN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num)))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_TIA_GAIN_0_LSB, RG_RX0_TIA_GAIN_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[0].gain_code = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_TIA_GAIN_0_LSB, RG_RX1_TIA_GAIN_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[1].gain_code = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_LED_DRIVER0_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_LED_DRIVER0_CODE_0_LSB, RG_LED_DRIVER0_CODE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->led_drv_code[0] = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_LED_DRIVER1_CODE_0_LSB, RG_LED_DRIVER1_CODE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->led_drv_code[1] = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_RX0_DC_CANCEL_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_DC_CANCEL_CODE_0_LSB, RG_RX0_DC_CANCEL_CODE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[0].dc_cancel_code = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_DC_CANCEL_CODE_0_LSB, RG_RX1_DC_CANCEL_CODE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[1].dc_cancel_code = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_RX0_DC_CANCEL_RANGE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_DC_CANCEL_RANGE_0_LSB, RG_RX0_DC_CANCEL_RANGE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[0].dc_cancel_range = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_DC_CANCEL_RANGE_0_LSB, RG_RX1_DC_CANCEL_RANGE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[1].dc_cancel_range = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_RX0_BG_CANCEL_RANGE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_BG_CANCEL_RANGE_0_LSB, RG_RX0_BG_CANCEL_RANGE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[0].bg_cancel_range = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_BG_CANCEL_RANGE_0_LSB, RG_RX1_BG_CANCEL_RANGE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_param[1].bg_cancel_range = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_RX_EN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX_EN_0_LSB, RG_RX_EN_0_MSB, &val);
        p_gh3036_ppg_cfg_param->rx_en = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_FIFO_CTRL_0_LSB, RG_FIFO_CTRL_0_MSB, &val);
        p_gh3036_ppg_cfg_param->fifo_ctrl = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_DRE_EN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_DRE_EN_0_LSB, RG_DRE_EN_0_MSB, &val);
        p_gh3036_ppg_cfg_param->dre_en = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_DRE_SCALE_0_LSB, RG_DRE_SCALE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->dre_scale = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_BG_LEVEL_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_BG_LEVEL_0_LSB, RG_BG_LEVEL_0_MSB, &val);
        p_gh3036_ppg_cfg_param->bg_level = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_SR_MULTIPLIER_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_SR_MULTIPLIER_0_LSB, RG_SR_MULTIPLIER_0_MSB, &val);
        p_gh3036_ppg_cfg_param->multiplier = val;
    }
    if (reg_addr == (uint16_t)(RG_DC_DAC_EN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_DC_DAC_EN_0_LSB, RG_DC_DAC_EN_0_MSB, &val);
        p_gh3036_ppg_cfg_param->dc_cancel_en = (uint8_t)val;
    }
    if (reg_addr == (uint16_t)(RG_DRE_FIFO_OUTPUT_MODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_DRE_FIFO_OUTPUT_MODE_0_LSB, RG_DRE_FIFO_OUTPUT_MODE_0_MSB, &val);
        p_gh3036_ppg_cfg_param->dre_fifo_output_mode = (uint8_t)val;
    }
    return ret;
}

uint32_t gh3036_ppg_multiplier_get(uint16_t reg_addr, uint16_t reg_val, uint16_t* p_multiplier, uint8_t multiplier_size)
{
    uint8_t slot_cfg_num;
    if (RG_SR_MULTIPLIER_0_ADDR <= reg_addr && reg_addr <= RG_SR_MULTIPLIER_7_ADDR)
    {
        slot_cfg_num = (reg_addr - RG_SR_MULTIPLIER_0_ADDR) / GH3036_REG_WIDTH;
        if (slot_cfg_num < multiplier_size)
        {
            gh_hal_get_reg_field(reg_val, RG_SR_MULTIPLIER_0_LSB, RG_SR_MULTIPLIER_0_MSB, p_multiplier + slot_cfg_num);
        }
    }
    return 0;
}

#if GH_HAL_AGC_DRE_EN
uint32_t gh3036_agc_reg_parse(uint16_t reg_addr,   uint16_t reg_val, uint8_t slot_cfg_num,
                              uint8_t* gain0_code, uint8_t* gain1_code,
                              uint8_t* led_drv0_code, uint8_t* led_drv1_code,
                              uint8_t* dc_cancel_code0, uint8_t* dc_cancel_code1,
                              uint8_t* dre_enable)
{
    uint32_t ret = 0;
    uint16_t val = 0;
    if (reg_addr == (uint16_t)(RG_RX0_TIA_GAIN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_TIA_GAIN_0_LSB, RG_RX0_TIA_GAIN_0_MSB, &val);
        *gain0_code = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_TIA_GAIN_0_LSB, RG_RX1_TIA_GAIN_0_MSB, &val);
        *gain1_code = (uint8_t)val;
    }
    else if (reg_addr == (uint16_t)(RG_RX0_DC_CANCEL_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_DC_CANCEL_CODE_0_LSB, RG_RX0_DC_CANCEL_CODE_0_MSB, &val);
        *dc_cancel_code0 = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_DC_CANCEL_CODE_0_LSB, RG_RX1_DC_CANCEL_CODE_0_MSB, &val);
        *dc_cancel_code1 = (uint8_t)val;
    }
    else if (reg_addr == (uint16_t)(RG_LED_DRIVER0_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_LED_DRIVER0_CODE_0_LSB, RG_LED_DRIVER0_CODE_0_MSB, &val);
        *led_drv0_code = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_LED_DRIVER1_CODE_0_LSB, RG_LED_DRIVER1_CODE_0_MSB, &val);
        *led_drv1_code = (uint8_t)val;
    }
    else if (reg_addr == (uint16_t)(RG_DRE_EN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_DRE_EN_0_LSB, RG_DRE_EN_0_MSB, &val);
        *dre_enable = (uint8_t)val;
    }
    return ret;
}
#else

uint32_t gh3036_agc_reg_parse(uint16_t reg_addr,   uint16_t reg_val, uint8_t slot_cfg_num,
                              uint8_t* gain0_code, uint8_t* gain1_code,
                              uint8_t* led_drv0_code, uint8_t* led_drv1_code)
{
    uint32_t ret = 0;
    uint16_t val = 0;
    if (reg_addr == (uint16_t)(RG_RX0_TIA_GAIN_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_RX0_TIA_GAIN_0_LSB, RG_RX0_TIA_GAIN_0_MSB, &val);
        *gain0_code = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_RX1_TIA_GAIN_0_LSB, RG_RX1_TIA_GAIN_0_MSB, &val);
        *gain1_code = (uint8_t)val;
    }
    else if (reg_addr == (uint16_t)(RG_LED_DRIVER0_CODE_0_ADDR + GH3036_REG_WIDTH * slot_cfg_num))
    {
        gh_hal_get_reg_field(reg_val, RG_LED_DRIVER0_CODE_0_LSB, RG_LED_DRIVER0_CODE_0_MSB, &val);
        *led_drv0_code = (uint8_t)val;
        gh_hal_get_reg_field(reg_val, RG_LED_DRIVER1_CODE_0_LSB, RG_LED_DRIVER1_CODE_0_MSB, &val);
        *led_drv1_code = (uint8_t)val;
    }

    return ret;
}
#endif

uint32_t gh_hal_param_update_start(void)
{
    uint32_t ret = 0;

    ret |= gh_hal_reg_bit_field_write(RG_PARAM_WRITE_START_ADDR, RG_PARAM_WRITE_START_LSB, RG_PARAM_WRITE_START_MSB, 1);

    return ret;
}

uint32_t gh_hal_param_update_end(void)
{
    uint32_t ret = 0;

    ret |= gh_hal_reg_bit_field_write(RG_PARAM_WRITE_END_ADDR, RG_PARAM_WRITE_START_LSB, RG_PARAM_WRITE_START_MSB, 1);

    return ret;
}

uint32_t gh_hal_led_drv_write(uint8_t slotcfg_idx, uint8_t drv_idx, uint8_t drv_code)
{
    uint32_t ret = 0;
    uint16_t reg_addr = (RG_LED_DRIVER0_CODE_0_ADDR + GH3036_REG_WIDTH * slotcfg_idx);
    if (GH_LED_DRV0_IDX == drv_idx)
    {
        ret |= gh_hal_reg_bit_field_write(reg_addr, RG_LED_DRIVER0_CODE_0_LSB, RG_LED_DRIVER0_CODE_0_MSB, drv_code);
    }
    else if (GH_LED_DRV1_IDX == drv_idx)
    {
        ret |= gh_hal_reg_bit_field_write(reg_addr, RG_LED_DRIVER1_CODE_0_LSB, RG_LED_DRIVER1_CODE_0_MSB, drv_code);
    }

    return ret;
}

uint32_t gh_hal_tia_gain_write(uint8_t slotcfg_idx, uint8_t rx_idx, uint8_t gain_code)
{
    uint32_t ret = 0;
    uint16_t reg_addr = (RG_RX0_TIA_GAIN_0_ADDR + GH3036_REG_WIDTH * slotcfg_idx);
    if (GH_TIA_GIAN0_IDX == rx_idx)
    {
        ret |= gh_hal_reg_bit_field_write(reg_addr, RG_RX0_TIA_GAIN_0_LSB, RG_RX0_TIA_GAIN_0_MSB, gain_code);
    }
    else if (GH_TIA_GIAN1_IDX == rx_idx)
    {
        ret |= gh_hal_reg_bit_field_write(reg_addr, RG_RX1_TIA_GAIN_0_LSB, RG_RX1_TIA_GAIN_0_MSB, gain_code);
    }

    return ret;
}



uint32_t gh_hal_chip_reset(void)
{
#if GH_SUPPORT_HARD_RESET
    uint32_t ret = gh_hard_reset();
#else
    uint32_t ret = gh_soft_reset();
#endif
    gh_hal_isr_status_t isr_status;
    ret |= gh_get_irq(&isr_status);
    return ret;
}

uint32_t gh3036_chip_ready(void)
{
    uint16_t reg_data = 0;
    uint8_t cnt = GH3036_CHIP_READY_CONFIRM_CNT;
    do
    {
        gh_hal_reg_bit_field_read(RG_CHIP_READY_CODE_ADDR, RG_CHIP_READY_CODE_LSB,
                                  RG_CHIP_READY_CODE_MSB, &reg_data);
        if (GH3036_CHIP_READY_VAL == reg_data)
        {
            return (uint32_t)GH_HAL_OK;
        }
        gh_hal_delay_ms(1);
        --cnt;
    } while (cnt);

    return (uint32_t)GH_HAL_INIT_FAIL;
}

uint32_t gh_hal_interface_init(void)
{
    uint32_t ret = 0;
#if GH_SUPPORT_HARD_RESET
    ret |= gh_hal_reset_pin_init();
#endif
    ret |= gh_hal_chip_reset();

    ret |= gh3036_chip_ready();

#if (GH_ISR_MODE == INTERRUPT_MODE)
    ret |= gh_hal_int_pin_init();
#endif


    return ret;
}
