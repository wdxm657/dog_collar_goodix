/**
 * @file ble_log.h
 * @brief Real-time BLE log output — sends text directly via LMS DATA Notify,
 *        bypassing flash storage. Requires LMS service initialized.
 */
#ifndef __BLE_LOG_H__
#define __BLE_LOG_H__

#include <stdint.h>
#include "custom_config.h"

/**
 * @brief Print formatted log string via BLE Notify (LMS DATA channel).
 *        Safe to call only when BLE is connected and LMS subscribed.
 *        No flash write involved.
 */
void ble_printf(const char *fmt, ...);

#endif
