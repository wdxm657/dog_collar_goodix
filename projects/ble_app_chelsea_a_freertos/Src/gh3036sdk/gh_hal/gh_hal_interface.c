#include <stdint.h>
#include "gh_hal_config.h"
#include "gh_hal_interface.h"
#include  "gh_hal_utils.h"
#include  "gh_hal_user.h"

/*
 * DEFINES
 *****************************************************************************************
 */
/* spi operation */
#define GH_SPI_CMD_BUFFER_LEN            (1)          /**< spi cmd buffer len */
#define GH_SPI_WRITE_REG_BUFFER_LEN      (5 + 20)     /**< spi write reg buffer len */
#define GH_SPI_WRITE_CMD_BUFFER_LEN      (3)          /**< spi write reg buffer len */

#define GH_SPI_READ_REG_BUFFER_LEN       (3)          /**< spi read reg buffer len */
#define GH_SPI_READ_REG_RX_BUFFER_LEN    (1 + 40)     /**< spi read reg rx buffer len */
#define GH_SPI_READ_REG_CMD_LEN          (1)          /**< spi read reg buffer len */
#define GH_SPI_READ_REG_DATA_LEN         (2)          /**< spi read reg buffer len */

// special reg addr
#define GH_FIFO_REG_ADDR                 (0xAAAA)     /**< fifo reg addr */

/// spi seq write cmd val
#define GH_SPI_CMD_WRITE                 (0xF0)

/// spi seq read cmd val
#define GH_SPI_CMD_READ                  (0xF1)

/// spi seq wait delay, 4us
#define GH_SPI_WAIT_DELAY_X_US           (1)

/// spi seq end delay, 2us
#define GH_SPI_END_DELAY_X_US            (2)

/// send cmd delay, 500us
#define GH_CMD_DELAY_X_US                (500)

/* index of interface buffer */
#define INTERFACE_INDEX_0_BUFFER         (0)         /**< index 0 of buffer */
#define INTERFACE_INDEX_1_BUFFER         (1)         /**< index 1 of buffer */
#define INTERFACE_INDEX_2_BUFFER         (2)         /**< index 2 of buffer */
#define INTERFACE_INDEX_3_BUFFER         (3)         /**< index 3 of buffer */
#define INTERFACE_INDEX_4_BUFFER         (4)         /**< index 4 of buffer */
#define INTERFACE_INDEX_5_BUFFER         (5)         /**< index 5 of buffer */
#define INTERFACE_INDEX_6_BUFFER         (6)         /**< index 6 of buffer */
#define INTERFACE_INDEX_7_BUFFER         (7)         /**< index 7 of buffer */

/* i2c operation */
#define I2C_CMD_BUFFER_LEN               (3)         /**< i2c cmd buffer len */
#define I2C_WRITE_REG_BUFFER_LEN         (2 + 20)    /**< i2c write reg buffer len */
#define I2C_READ_REG_ADDR_LEN            (2)         /**< i2c read reg buffer len */
#define I2C_READ_REG_DATA_LEN            (2)         /**< i2c read reg buffer len */
#define GH_I2C_CMD_ADDR                  (0xDDDD)    /**< i2c cmd reg addr */
#define GH_I2C_DEVICE_ID_BASE            (0x28)
#define GH_I2C_DEVICE_ID                 (GH_I2C_DEVICE_ID_BASE | (GH_I2C_DEVICE_ID_SEL << 1))

#define RETURN_VALUE_ASSEMBLY(internal_err, interface_err) \
    (((internal_err) << 8) | (interface_err))

/**
  * @brief sleep status
  */
static volatile gh_hal_sleep_state_e g_sleep_status = GH_STATUS_SLEPP;
static volatile uint8_t g_interface_type = GH_INTERFACE_TYPE;


static void gh_hal_swap_endian(uint8_t* p_data_buffer, uint16_t data_len);
static uint32_t gh_try_wake_up(void);

extern uint32_t gh_hal_i2c_init(void);
extern uint32_t gh_hal_i2c_write(uint8_t i2c_slaver_id, uint8_t* p_buffer, uint16_t len);
extern uint32_t gh_hal_i2c_read(uint8_t i2c_slaver_id, uint8_t* p_buffer, uint16_t len);
extern uint32_t gh_hal_spi_write_read(uint8_t* p_tx_buffer, uint8_t* p_rx_buffer, uint16_t len);

void gh_hal_set_interface_type(uint8_t interface_type)
{
    if (interface_type > GH_INTERFACE_SPI_HW_CS)
    {
        return;
    }
    g_interface_type = interface_type;
}

uint8_t gh_hal_get_interface_type(void)
{
    return g_interface_type;
}

/**
 * @fn     void gh_get_sleep_status(void)
 *
 * @brief  set sleep status
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  sleep status
 */
static gh_hal_sleep_state_e gh_get_sleep_status(void)
{
    return g_sleep_status;
}

/**
 * @fn     void gh_set_sleep_status(gh_hal_sleep_state_e sleep_status)
 *
 * @brief  set sleep status
 *
 * @attention   None
 *
 * @param[in]   sleep_status      current sleep status
 * @param[out]  None
 *
 * @return  None
 */
static void gh_set_sleep_status(gh_hal_sleep_state_e sleep_status)
{
    g_sleep_status = sleep_status;
}


/**
 * @fn     uint32_t gh_hal_cmd_write(uint8_t cmd);
 *
 * @brief  Write command.
 *
 * @attention   None
 *
 * @param[in]   cmd              command to write
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_cmd_write_soft_cs(uint8_t cmd)
{
    // step1: set cs low
    uint32_t ret = gh_hal_spi_cs_ctrl(GH_IO_SET_LOW);

    // step2: write value

    ret |= gh_hal_spi_write(&cmd, 1);

    // step3: delay
    ret |= gh_hal_delay_us(GH_SPI_WAIT_DELAY_X_US);

    // step4: set cs high
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_HIGH);

    // step5: delay
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);

    return ret;
}

/**
 * @fn     uint32_t gh_hal_spi_regs_write_soft_cs(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num);
 *
 * @brief  register burst write function
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register address
 * @param[in]   p_reg_values      pointer of the array containing values for writing.
 * @param[in]   regs_num          number of registers for writing.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_regs_write_soft_cs(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num)
{
    if (GH_NULL_PTR == p_reg_values || 0 == regs_num)
    {
        return (uint32_t)GH_HAL_PTR_NULL;
    }

    uint32_t ret = 0;
    uint8_t buffer[GH_SPI_WRITE_REG_BUFFER_LEN];
    buffer[INTERFACE_INDEX_0_BUFFER] = GH_SPI_CMD_WRITE;
    buffer[INTERFACE_INDEX_1_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(reg_addr);
    buffer[INTERFACE_INDEX_2_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(reg_addr);
    buffer[INTERFACE_INDEX_3_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(regs_num * sizeof(uint16_t));
    buffer[INTERFACE_INDEX_4_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(regs_num * sizeof(uint16_t));

    // step1: set cs low
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_LOW);

    // step2: write value
    for (uint16_t i = 0; i < regs_num; i++)
    {
        buffer[INTERFACE_INDEX_5_BUFFER + sizeof(uint16_t) * i] = GH_GET_HIGH_BYTE_FROM_WORD(p_reg_values[i]);
        buffer[INTERFACE_INDEX_5_BUFFER + sizeof(uint16_t) * i + 1] = GH_GET_LOW_BYTE_FROM_WORD(p_reg_values[i]);
    }
    ret |= gh_hal_spi_write(buffer, INTERFACE_INDEX_5_BUFFER + regs_num * sizeof(uint16_t));

    // step3: delay
    ret |= gh_hal_delay_us(GH_SPI_WAIT_DELAY_X_US);

    // step4: set cs high
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_HIGH);

    // step5: delay
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);

    return ret;
}

/**
 * @fn    gh_hal_spi_bytes_read_soft_cs(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len)
 *
 * @brief   burst read bytes
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register begin address.
 * @param[in]   p_buffer          pointer to read buffer.
 * @param[in]   len               read length.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_bytes_read_soft_cs(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return (uint32_t)GH_HAL_PTR_NULL;
    }

    uint16_t ret = 0;
    uint8_t buffer[GH_SPI_READ_REG_BUFFER_LEN];

    // step1: set cs low
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_LOW);

    // step2: write reg
    buffer[INTERFACE_INDEX_0_BUFFER] = GH_SPI_CMD_WRITE;
    buffer[INTERFACE_INDEX_1_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(reg_addr);
    buffer[INTERFACE_INDEX_2_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(reg_addr);
    ret |= gh_hal_spi_write(buffer, GH_SPI_READ_REG_BUFFER_LEN);

    // step3: delay
    ret |= gh_hal_delay_us(GH_SPI_WAIT_DELAY_X_US);

    // step4: set cs high
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_HIGH);

    // step5: delay
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);

    // step6: set cs low
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_LOW);

    // step7: write read cmd
    buffer[INTERFACE_INDEX_0_BUFFER] = GH_SPI_CMD_READ;
    ret |= gh_hal_spi_write(buffer, GH_SPI_READ_REG_CMD_LEN);

    // step8: read reg
    ret |= gh_hal_spi_read(p_buffer, len);

    // step9: delay
    ret |= gh_hal_delay_us(GH_SPI_WAIT_DELAY_X_US);

    // step10: set cs high
    ret |= gh_hal_spi_cs_ctrl(GH_IO_SET_HIGH);

    // step11: delay
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);
    return ret;
}

/**
 * @fn     static uint32_t gh_hal_spi_regs_read_soft_cs(uint16_t reg_addr,
 *                                                    uint16_t* p_reg_buffer,
 *                                                    uint16_t regs_num)
 *
 * @brief   burst read registers
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register begin address.
 * @param[in]   p_reg_buffer      pointer to register buffer.
 * @param[in]   regs_num          number of registers to read.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_regs_read_soft_cs(uint16_t reg_addr, uint16_t* p_reg_buffer, uint16_t regs_num)
{
    if (GH_NULL_PTR == p_reg_buffer || 0 == regs_num)
    {
        return (uint32_t)GH_HAL_PTR_NULL;
    }
    uint16_t ret = 0;
    ret |= gh_hal_spi_bytes_read_soft_cs(reg_addr, (uint8_t *)p_reg_buffer, regs_num * sizeof(p_reg_buffer[0]));
    gh_hal_swap_endian((uint8_t *)p_reg_buffer, regs_num * sizeof(p_reg_buffer[0]));
    return ret;
}

/**
 * @fn     uint32_t gh_hal_spi_fifo_read_soft_cs(uint8_t* p_buffer, uint16_t len);
 *
 * @brief  read data from fifo
 *
 * @attention   None
 *
 * @param[in]   p_buffer          pointer to buffer for read data
 * @param[in]   len               length of data to read
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_fifo_read_soft_cs(uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return (uint32_t)GH_HAL_PTR_NULL;
    }
    uint16_t ret = 0;
    ret |= gh_hal_spi_bytes_read_soft_cs(GH_FIFO_REG_ADDR, (uint8_t *)p_buffer, len);
    return ret;
}

static uint32_t gh_hal_spi_regs_write_hw_cs(uint16_t reg_addr, uint16_t* p_reg_value, uint16_t regs_num);
static uint32_t gh_hal_spi_regs_read_hw_cs(uint16_t reg_addr, uint16_t* p_reg_buffer, uint16_t regs_num);
static uint32_t gh_hal_spi_bytes_read_hw_cs(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len);

/**
 * @fn     uint32_t gh_hal_cmd_write(uint8_t cmd);
 *
 * @brief  Write command.
 *
 * @attention   None
 *
 * @param[in]   cmd              command to write
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_cmd_write_hw_cs(uint8_t cmd)
{
    uint32_t ret = 0;
    ret |= gh_hal_spi_write(&cmd, 1);
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);
    return ret;
}

/**
 * @fn     uint32_t gh_hal_spi_regs_write_hw_cs(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num);
 *
 * @brief  register burst write function
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register address
 * @param[in]   p_reg_values      pointer of the array containing values for writing.
 * @param[in]   regs_num          number of registers for writing.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_regs_write_hw_cs(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num)
{
    if (GH_NULL_PTR == p_reg_values || 0 == regs_num)
    {
        return GH_HAL_PTR_NULL;
    }
    uint32_t ret = 0;
    uint8_t buffer[GH_SPI_WRITE_REG_BUFFER_LEN];
    buffer[INTERFACE_INDEX_0_BUFFER] = GH_SPI_CMD_WRITE;
    buffer[INTERFACE_INDEX_1_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(reg_addr);
    buffer[INTERFACE_INDEX_2_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(reg_addr);
    buffer[INTERFACE_INDEX_3_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(regs_num * sizeof(uint16_t));
    buffer[INTERFACE_INDEX_4_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(regs_num * sizeof(uint16_t));
    for (uint16_t i = 0; i < regs_num; i++)
    {
        buffer[INTERFACE_INDEX_5_BUFFER + sizeof(uint16_t) * i] = GH_GET_HIGH_BYTE_FROM_WORD(p_reg_values[i]);
        buffer[INTERFACE_INDEX_5_BUFFER + sizeof(uint16_t) * i + 1] = GH_GET_LOW_BYTE_FROM_WORD(p_reg_values[i]);
    }
    ret |= gh_hal_spi_write(buffer, INTERFACE_INDEX_5_BUFFER + regs_num * sizeof(uint16_t));
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);
    return ret;
}

/**
 * @fn    uint32_t gh_hal_wrtie_F1_and_read(uint8_t* p_buffer, uint16_t len)
 *
 * @brief   spi writr one byte "0xF1" and read bytes
 *
 * @attention   None
 *
 * @param[in]   p_buffer          pointer to read buffer.
 * @param[in]   len               read length.
 * @param[out]  None
 *
 * @return  error code
 */
uint32_t gh_hal_wrtie_F1_and_read(uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return GH_HAL_PTR_NULL;
    }
    uint32_t ret = 0;
    p_buffer[0] = GH_SPI_CMD_READ;
    ret |= gh_hal_spi_write_read(p_buffer, p_buffer, len + 1);
    return ret;
}

/**
 * @fn    gh_hal_spi_bytes_read_hw_cs(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len)
 *
 * @brief   burst read bytes
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register begin address.
 * @param[in]   p_buffer          pointer to read buffer.
 * @param[in]   len               read length.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_bytes_read_hw_cs(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return GH_HAL_PTR_NULL;
    }
    uint16_t ret = 0;
    uint8_t buffer[GH_SPI_READ_REG_BUFFER_LEN];
    buffer[INTERFACE_INDEX_0_BUFFER] = GH_SPI_CMD_WRITE;
    buffer[INTERFACE_INDEX_1_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(reg_addr);
    buffer[INTERFACE_INDEX_2_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(reg_addr);
    ret |= gh_hal_spi_write(buffer, GH_SPI_READ_REG_BUFFER_LEN);
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);
    gh_hal_wrtie_F1_and_read(p_buffer, len);
    ret |= gh_hal_delay_us(GH_SPI_END_DELAY_X_US);
    return ret;
}

/**
 * @fn     static uint32_t gh_hal_spi_regs_read_hw_cs(uint16_t reg_addr,
 *                                                    uint16_t* p_reg_buffer,
 *                                                    uint16_t regs_num)
 *
 * @brief   burst read registers
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register begin address.
 * @param[in]   p_reg_buffer      pointer to register buffer.
 * @param[in]   regs_num          number of registers to read.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_regs_read_hw_cs(uint16_t reg_addr, uint16_t* p_reg_buffer, uint16_t regs_num)
{
    if (GH_NULL_PTR == p_reg_buffer || 0 == regs_num)
    {
        return GH_HAL_PTR_NULL;
    }
    uint16_t ret = 0;
    uint8_t gh_spi_read_reg_rx_buffer[GH_SPI_READ_REG_RX_BUFFER_LEN];
    uint8_t max_read_reg_num = (GH_SPI_READ_REG_RX_BUFFER_LEN - 1) / sizeof(p_reg_buffer[0]);

    regs_num = (regs_num > max_read_reg_num) ? max_read_reg_num : regs_num;
    ret |= gh_hal_spi_bytes_read_hw_cs(reg_addr,
                                       (uint8_t *)gh_spi_read_reg_rx_buffer,
                                       regs_num * sizeof(p_reg_buffer[0]));

    gh_memcpy((uint8_t *)p_reg_buffer, gh_spi_read_reg_rx_buffer + 1, regs_num * sizeof(p_reg_buffer[0]));
    gh_hal_swap_endian((uint8_t *)p_reg_buffer, regs_num * sizeof(p_reg_buffer[0]));

    return ret;
}

/**
 * @fn     static uint32_t gh_hal_spi_fifo_read_hw_cs(uint8_t* p_buffer, uint16_t len)
 *
 * @brief  read data from fifo registe
 *
 * @attention   None
 *
 * @param[in]   p_buffer          pointer to buffer where data will be stored in.
 * @param[in]   len               number of bytes to read from fifo register.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_spi_fifo_read_hw_cs(uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return GH_HAL_PTR_NULL;
    }
    uint16_t ret = 0;
    ret |= gh_hal_spi_bytes_read_hw_cs(GH_FIFO_REG_ADDR, (uint8_t *)p_buffer, len);
    return ret;
}



/**
 * @fn     static void GH3X2X_I2cSendCmd(GU8 uchCmd)
 *
 * @brief  Send cmd via i2c
 *
 * @attention   None
 *
 * @param[in]   uchCmd      i2c cmd
 * @param[out]  None
 *
 * @return  None
 */
static uint32_t gh_hal_i2c_cmd_write(uint8_t cmd)
{
    uint8_t buf[I2C_CMD_BUFFER_LEN] = {0};

    buf[INTERFACE_INDEX_0_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(GH_I2C_CMD_ADDR);
    buf[INTERFACE_INDEX_1_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(GH_I2C_CMD_ADDR);
    buf[INTERFACE_INDEX_2_BUFFER] = cmd;
    gh_hal_i2c_write(GH_I2C_DEVICE_ID, buf, I2C_CMD_BUFFER_LEN);

    return 0;
}

/**
 * @fn     uint32_t gh_hal_i2c_regs_write(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num);
 *
 * @brief  register burst write function
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register address
 * @param[in]   p_reg_values      pointer of the array containing values for writing.
 * @param[in]   regs_num          number of registers for writing.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_i2c_regs_write(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num)
{
    uint16_t i;
    uint8_t buf[I2C_WRITE_REG_BUFFER_LEN] = {0};

    buf[INTERFACE_INDEX_0_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(reg_addr);
    buf[INTERFACE_INDEX_1_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(reg_addr);

    for (i = 0; i < regs_num; i++)
    {
        if (INTERFACE_INDEX_3_BUFFER + sizeof(uint16_t) * i < I2C_WRITE_REG_BUFFER_LEN)
        {
            buf[INTERFACE_INDEX_2_BUFFER + sizeof(uint16_t) * i] = GH_GET_HIGH_BYTE_FROM_WORD(p_reg_values[i]);
            buf[INTERFACE_INDEX_3_BUFFER + sizeof(uint16_t) * i] = GH_GET_LOW_BYTE_FROM_WORD(p_reg_values[i]);
        }
        else
        {
            i--;
            break;
        }
    }

    gh_hal_i2c_write(GH_I2C_DEVICE_ID, buf, I2C_READ_REG_ADDR_LEN + sizeof(uint16_t) * i);
    return 0;
}


/**
 * @fn    gh_hal_i2c_bytes_read(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len)
 *
 * @brief   burst read bytes
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register begin address.
 * @param[in]   p_buffer          pointer to read buffer.
 * @param[in]   len               read length.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_i2c_bytes_read(uint16_t reg_addr, uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return GH_HAL_PTR_NULL;
    }
    uint32_t ret = 0;
    uint8_t cmd_buffer[I2C_READ_REG_ADDR_LEN] = {0};

    cmd_buffer[INTERFACE_INDEX_0_BUFFER] = GH_GET_HIGH_BYTE_FROM_WORD(reg_addr);
    cmd_buffer[INTERFACE_INDEX_1_BUFFER] = GH_GET_LOW_BYTE_FROM_WORD(reg_addr);
    gh_hal_i2c_write(GH_I2C_DEVICE_ID, cmd_buffer, I2C_READ_REG_ADDR_LEN);
    gh_hal_i2c_read(GH_I2C_DEVICE_ID, p_buffer, len);
    return ret;
}

/**
 * @fn     static uint32_t gh_hal_i2c_regs_read(uint16_t reg_addr,
 *                                              uint16_t* p_reg_buffer,
 *                                              uint16_t regs_num)
 *
 * @brief   burst read registers
 *
 * @attention   None
 *
 * @param[in]   reg_addr          register begin address.
 * @param[in]   p_reg_buffer      pointer to register buffer.
 * @param[in]   regs_num          number of registers to read.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_i2c_regs_read(uint16_t reg_addr, uint16_t* p_reg_buffer, uint16_t regs_num)
{
    uint32_t ret = gh_hal_i2c_bytes_read(reg_addr, (uint8_t*) p_reg_buffer, regs_num * sizeof(uint16_t));
    gh_hal_swap_endian((uint8_t*)p_reg_buffer, regs_num * sizeof(p_reg_buffer[0]));
    return ret;
}

/**
 * @fn     static uint32_t gh_hal_i2c_fifo_read(uint8_t* p_buffer, uint16_t len)
 *
 * @brief  read data from fifo registe
 *
 * @attention   None
 *
 * @param[in]   p_buffer          pointer to buffer where data will be stored in.
 * @param[in]   len               number of bytes to read from fifo register.
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_hal_i2c_fifo_read(uint8_t* p_buffer, uint16_t len)
{
    if (GH_NULL_PTR == p_buffer || 0 == len)
    {
        return GH_HAL_PTR_NULL;
    }
    uint16_t ret = 0;
    ret |= gh_hal_i2c_bytes_read(GH_FIFO_REG_ADDR, p_buffer, len);
    return ret;
}

uint32_t gh_hal_regs_read(uint16_t reg_addr, uint16_t* p_reg_buffer, uint16_t regs_num)
{
    uint32_t ret = gh_try_wake_up();

    if (GH_INTERFACE_SPI_SW_CS == g_interface_type)
    {
        ret |= gh_hal_spi_regs_read_soft_cs(reg_addr, p_reg_buffer, regs_num);
    }
    else if (GH_INTERFACE_SPI_HW_CS == g_interface_type)
    {
        ret |= gh_hal_spi_regs_read_hw_cs(reg_addr, p_reg_buffer, regs_num);
    }
    else if (GH_INTERFACE_I2C == g_interface_type)
    {
        ret |= gh_hal_i2c_regs_read(reg_addr, p_reg_buffer, regs_num);
    }
    return ret;
}

uint32_t gh_hal_reg_read(uint16_t reg_addr, uint16_t* p_reg_val)
{
    return gh_hal_regs_read(reg_addr, p_reg_val, 1);
}

uint32_t gh_hal_fifo_read(uint8_t* p_buffer, uint16_t len)
{
    uint32_t ret = gh_try_wake_up();

    if (GH_INTERFACE_SPI_SW_CS == g_interface_type)
    {
        ret |= gh_hal_spi_fifo_read_soft_cs(p_buffer, len);
    }
    else if (GH_INTERFACE_SPI_HW_CS == g_interface_type)
    {
        ret |=  gh_hal_spi_fifo_read_hw_cs(p_buffer, len);
    }
    else if (GH_INTERFACE_I2C == g_interface_type)
    {
        ret |=  gh_hal_i2c_fifo_read(p_buffer, len);
    }
    return ret;
}

uint32_t gh_hal_regs_write(uint16_t reg_addr, uint16_t* p_reg_values, uint16_t regs_num)
{
    uint32_t ret = gh_try_wake_up();

    if (GH_INTERFACE_SPI_SW_CS == g_interface_type)
    {
        ret |= gh_hal_spi_regs_write_soft_cs(reg_addr, p_reg_values, regs_num);
    }
    else if (GH_INTERFACE_SPI_HW_CS == g_interface_type)
    {
        ret |=  gh_hal_spi_regs_write_hw_cs(reg_addr, p_reg_values, regs_num);
    }
    else if (GH_INTERFACE_I2C == g_interface_type)
    {
        ret |=  gh_hal_i2c_regs_write(reg_addr, p_reg_values, regs_num);
    }
    return ret;
}

uint32_t gh_hal_reg_write(uint16_t reg_addr, uint16_t reg_value)
{
    return gh_hal_regs_write(reg_addr, &reg_value, 1);
}

uint32_t gh_hal_cmd_write(uint8_t cmd)
{
    if (GH_INTERFACE_SPI_SW_CS == g_interface_type)
    {
        return gh_hal_spi_cmd_write_soft_cs(cmd);
    }
    else if (GH_INTERFACE_SPI_HW_CS == g_interface_type)
    {
        return gh_hal_spi_cmd_write_hw_cs(cmd);
    }
    else if (GH_INTERFACE_I2C == g_interface_type)
    {
        return gh_hal_i2c_cmd_write(cmd);
    }
    return 1;
}

uint32_t gh_hal_reg_bit_field_write(uint16_t reg_addr, uint8_t lsb, uint8_t msb, uint16_t value)
{
    uint16_t mask_data = ((((uint16_t)0x0001) << (msb - lsb + 1)) - 1) << lsb;
    uint16_t reg_data = 0;
    uint32_t ret = gh_hal_reg_read(reg_addr, &reg_data);
    GH_VAL_CLEAR_BIT(reg_data, mask_data);
    GH_VAL_SET_BIT(reg_data, (value << lsb) & mask_data);
    ret |= gh_hal_reg_write(reg_addr, reg_data);
    return ret;
}

uint32_t gh_hal_get_reg_field(uint16_t reg_val, uint8_t lsb, uint8_t msb, uint16_t* p_value)
{
    uint16_t mask_data = ((((uint16_t)0x0001) << (msb - lsb + 1)) - 1) << lsb;
    GH_VAL_GET_BIT(reg_val, mask_data);
    *p_value = (reg_val >> lsb);

    return 0;
}

uint32_t gh_hal_reg_bit_field_read(uint16_t reg_addr, uint8_t lsb, uint8_t msb, uint16_t* p_value)
{
    uint16_t mask_data = ((((uint16_t)0x0001) << (msb - lsb + 1)) - 1) << lsb;
    uint16_t reg_data = 0;
    uint32_t ret = gh_hal_reg_read(reg_addr, &reg_data);

    GH_VAL_GET_BIT(reg_data, mask_data);
    *p_value = (reg_data >> lsb);

    return ret;
}

uint32_t gh_exit_lowpower_mode(void)
{
    uint32_t ret = gh_hal_cmd_write(GH_CMD_WAKE_UP);

    gh_hal_delay_us(GH_CMD_WAKE_UP_WAIT_TIME);
    gh_set_sleep_status(GH_STATUS_WAKE_UP);

    return ret;
}

uint32_t gh_enter_lowpower_mode(void)
{
    uint32_t ret = gh_hal_cmd_write(GH_CMD_SLEEP);

    gh_set_sleep_status(GH_STATUS_SLEPP);

    return ret;
}

/**
 * @fn     uint32_t gh_try_wake_up(void)
 *
 * @brief  try wake up according to sleep status
 *
 * @attention   None
 *
 * @param[in]   None
 * @param[out]  None
 *
 * @return  error code
 */
static uint32_t gh_try_wake_up(void)
{
    uint32_t ret = 0;

    if (GH_STATUS_SLEPP == gh_get_sleep_status())
    {
        ret = gh_exit_lowpower_mode();
    }

    return ret;
}

uint32_t gh_soft_reset(void)
{
    uint32_t ret = gh_hal_cmd_write(GH_CMD_RESET);

    gh_hal_delay_ms(GH_CMD_RESET_WAIT_TIME);
    gh_set_sleep_status(GH_STATUS_WAKE_UP);

    return ret;
}

#if GH_SUPPORT_HARD_RESET
uint32_t gh_hard_reset(void)
{
    uint32_t ret = 0;

    ret |= gh_hal_reset_pin_ctrl(GH_IO_SET_LOW);
    gh_hal_delay_ms(GH_HARD_RESET_PULSE_WIDTH);
    ret |= gh_hal_reset_pin_ctrl(GH_IO_SET_HIGH);

    gh_hal_delay_ms(GH_CMD_RESET_WAIT_TIME);
    gh_set_sleep_status(GH_STATUS_WAKE_UP);

    return ret;
}
#endif

/**
 * @fn    static void gh_hal_swap_endian(uint8_t* p_data_buffer, uint16_t data_len)
 *
 * @brief  Swap endian of the buffer.
 *
 * @attention   None
 *
 * @param[in]   p_data_buffer     data buffer
 * @param[in]   len               length in bytes to swap
 * @param[out]  None
 *
 * @return      None
 */
static void gh_hal_swap_endian(uint8_t* p_data_buffer, uint16_t data_len)
{
    uint16_t index = 0;
    uint8_t type_size = sizeof(uint16_t);
    for (index = 0; index < (data_len / type_size); index++)
    {
        p_data_buffer[index * type_size] ^= p_data_buffer[index * type_size + 1];
        p_data_buffer[index * type_size + 1] ^= p_data_buffer[index * type_size];
        p_data_buffer[index * type_size] ^= p_data_buffer[index * type_size + 1];
    }
}
