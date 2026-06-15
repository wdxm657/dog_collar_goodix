#include "gh_gsensor_bridge.h"

#include "gh_app.h"
#include "gh_public_api.h"
#include "gh_global_config.h"
#include "gh_hal_log.h"

#if GH_USER_LOG_EN
#define DEBUG_LOG(...)                      GH_LOG_LVL_DEBUG(__VA_ARGS__)
#define WARNING_LOG(...)                    GH_LOG_LVL_WARNING(__VA_ARGS__)
#define ERROR_LOG(...)                      GH_LOG_LVL_ERROR(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#define WARNING_LOG(...)
#define ERROR_LOG(...)
#endif

extern uint64_t gh_hal_get_timestamp();

static uint64_t gh_gsensor_bridge_get_timestamp(void)
{
    return gh_hal_get_timestamp();
}

void gh_gsensor_bridge_publish(const imu_data_t *data, uint8_t len)
{
    gh_gsensor_ts_and_data_t gsensor_data;

    if ((data == GH_NULL_PTR) || (len == 0))
    {
        return;
    }

    for (uint8_t i = 0; i < len; i++)
    {
        gsensor_data.timestamp = data[i].timestamp;
        gsensor_data.data.acc[GH_ACCX_IDX] = data[i].accel_x;
        gsensor_data.data.acc[GH_ACCY_IDX] = data[i].accel_y;
        gsensor_data.data.acc[GH_ACCZ_IDX] = data[i].accel_z;
        if (GH_API_OK != gh_demo_gsensor_data_set(&gsensor_data))
        {
            WARNING_LOG("gh_demo_gsensor_data_set failed, idx=%d", i);
        }
    }
}

void gh_gsensor_bridge_init(void)
{
    imu_register_publish_hook(gh_gsensor_bridge_publish);
    imu_register_timestamp_hook(gh_gsensor_bridge_get_timestamp);
}
