#ifndef GH_DEMO_H
#define GH_DEMO_H

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include "gh_global_config.h"
#include "gh_hal_isr.h"
#include "gh_hal_log.h"
#include "gh_hal_interface.h"
#include "gh_hal_config_process.h"
#include "gh_algo_adapter_common.h"
#include "gh_public_api.h"
#include "gh_app.h"
#include "gh_app_user.h"
#include "gh_hal_user.h"
#if (1 == GH_PROTOCOL_EN)
#include "gh_data_package.h"
#include "gh_protocol_user.h"
#endif
#include "gh_app_manager.h"


void gh_app_demo_init(void);

void gh_app_demo_deinit(void);

void gh_app_demo_start(uint32_t mode);

void gh_app_demo_stop(uint32_t mode);

void gh_app_demo_cfg_switch(uint8_t index);

void gh_app_demo_int_process(void);

void gh_app_demo_read_efuse(uint64_t efuse_256bit[4]);

/* 自动佩戴检测事件: 0=无事件, 1=wear_on, 2=wear_off */
extern volatile uint32_t g_adt_wear_event;

#endif /* GH_DEMO_H */
