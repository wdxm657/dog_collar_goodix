/**
 * @file ble_log.c
 * @brief Real-time BLE log output via dedicated LOG TX channel.
 *
 * Sends log text via HEALTH LOG TX characteristic (UUID 00000005-...),
 * completely separate from GH RPC data (UUID 00000003-...).
 * No flash storage involved.
 */
#include "ble_log.h"
#include "custom_config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "health.h"

#define BLE_LOG_BUF_SIZE 128

void ble_printf(const char *fmt, ...)
{
    char buf[BLE_LOG_BUF_SIZE];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len >= (int)sizeof(buf))
        {
            len = (int)sizeof(buf) - 1;
        }
        /* Send via dedicated LOG TX channel (UUID 00000005-...),
         * separate from GH RPC data (UUID 00000003-...). */
        health_log_data_send(0, (uint8_t *)buf, (uint16_t)len);
    }
}
