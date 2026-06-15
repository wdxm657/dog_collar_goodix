#ifndef __GH_GSENSOR_BRIDGE_H__
#define __GH_GSENSOR_BRIDGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "app_thread_imu.h"

void gh_gsensor_bridge_init(void);
void gh_gsensor_bridge_publish(const imu_data_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif
