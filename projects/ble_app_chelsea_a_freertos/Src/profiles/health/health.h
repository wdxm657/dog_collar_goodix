/**
 *****************************************************************************************
 *
 * @file health.h
 *
 * @brief Goodix UART Service API
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

/**
 * @addtogroup BLE_SRV BLE Services
 * @{
 * @brief Definitions and prototypes for the BLE Service interface.
 */

/**
 * @defgroup BLE_SDK_HEALTH Goodix UART Service (HEALTH)
 * @{
 * @brief Definitions and prototypes for the HEALTH interface.
 *
 * @details The Goodix UART Service is a customized GATT-based service with Tx, Rx and Flow Control
 *          characteristics. The application uses the service to send and receive data to and
 *          from the peer. The application data is sent to the peer as Handle Value Notification,
 *          and the data received from the peer is transmitted with GATT Write Command.
 *
 *          After \ref health_init_t variable is initialized , the application must call \ref health_service_init()
 *          to add the Goodix Uart Service and Rx, Tx, Flow Control characteristics to the BLE Stack
 *          database. The application can send the data to the peer with \ref health_tx_data_send() after
 *          \ref HEALTH_EVT_TX_PORT_OPENED received. The application should copy the received data to its own buffer
 *          when handling \ref HEALTH_EVT_RX_DATA_RECEIVED.
 */

#ifndef __HEALTH_H__
#define __HEALTH_H__

#include "gr_includes.h"
#include "custom_config.h"

/**
 * @defgroup HEALTH_MACRO Defines
 * @{
 */
#define HEALTH_CONNECTION_MAX      10                                                 /**< Maximum number of Goodix UART Service connections. */
#define HEALTH_MAX_DATA_LEN        247                                                /**< Maximum length of application data packet which is transmitted via HEALTH. */
#define HEALTH_FLOW_CTRL_LEN       1                                                  /**< Maximum length of ble flow control data packet which is transmitted via HEALTH. */
#define HEALTH_SERVICE_UUID        0xFB, 0x34, 0x9B, 0x5f, 0x80, 0x00, 0x00, 0x80, \
                                   0x00, 0x10, 0x00, 0x00, 0x0E, 0x19, 0x00, 0x00     /**< The UUID of Goodix UART Service for setting advertising data. */
/** @} */

/**
 * @defgroup HEALTH_ENUM Enumerations
 * @{
 */
/**@brief Goodix UART Service event types. */
typedef enum
{
    HEALTH_EVT_INVALID,                /**< Invalid HEALTH event. */
    HEALTH_EVT_RX_DATA_RECEIVED,       /**< The data from the peer has been received. */
    HEALTH_EVT_TX_DATA_SENT,           /**< The data from the application has been sent, and the service is ready to accept new data from the application. */
    HEALTH_EVT_TX_PORT_OPENED,         /**< Tx port has been opened. */
    HEALTH_EVT_TX_PORT_CLOSED,         /**< Tx port has been closed. */
    HEALTH_EVT_TX_FLOW_OFF,            /**< Tx flow off control request. */
    HEALTH_EVT_TX_FLOW_ON,             /**< Tx flow on control request. */
} health_evt_type_t;

/**@brief Flow control state for HEALTH service. */
enum health_flow_ctrl_state
{
  HEALTH_FLOW_CTRL_STATE_OFF = 0,      /**< Indicate that HEALTH can not receive data from peer. */
  HEALTH_FLOW_CTRL_STATE_ON            /**< Indicate that HEALTH can receive data from peer. */
};
/**@brief Underlying type used for the HEALTH flow control state. */
typedef uint8_t health_flow_ctrl_state_t;
/** @} */

/**
 * @defgroup HEALTH_STRUCT Structures
 * @{
 */
/**@brief Goodix UART Service event. */
typedef struct
{
    health_evt_type_t  evt_type;   /**< The HEALTH event. */
    uint8_t         conn_idx;   /**< The index of the connection for the data transmission. */
    uint8_t        *p_data;     /**< Pointer to the buffer within received data. */
    uint16_t        length;     /**< Length of received data. */
} health_evt_t;
/** @} */

/**
 * @addtogroup HEALTH_TYPEDEF Typedefs
 * @{
 */
/**@brief Goodix UART Service event handler type. */
typedef void (*health_evt_handler_t)(health_evt_t *p_evt);
/** @} */

/**
 * @addtogroup HEALTH_STRUCT Structures
 * @{
 */
/**@brief Goodix UART Service init stucture. This contains all option and data needed for initialization of the service. */
typedef struct
{
    health_evt_handler_t evt_handler;                     /**< Goodix UART Service event handler which must be provided by the application to send and receive the data. */
} health_init_t;
/** @} */

/**
 * @defgroup HEALTH_FUNCTION Functions
 * @{
 */
/**
 *****************************************************************************************
 * @brief Initialize a Goodix UART Service instance and add in the database.
 *
 * @param[in] p_health_init: Pointer to Goodix UART Service initialization variables.
 *
 * @return Result of service initialization.
 *****************************************************************************************
 */
sdk_err_t health_service_init(health_init_t *p_health_init);

/**
 *****************************************************************************************
 * @brief Send data to peer device.
 *
 * @param[in] conn_idx: Index of the connection.
 * @param[in] p_data:   Pointer to sent data.
 * @param[in] length:   Length of sent data.
 *
 * @return Result of sending data.
 *****************************************************************************************
 */
sdk_err_t health_tx_data_send(uint8_t conn_idx, uint8_t *p_data, uint16_t length);

/**
 *****************************************************************************************
 * @brief Provide the interface for other modules to obtain the health service start handle .
 *
 * @return The health service start handle.
 *****************************************************************************************
 */
uint16_t health_service_start_handle_get(void);

/** @} */

#endif
/** @} */
/** @} */
