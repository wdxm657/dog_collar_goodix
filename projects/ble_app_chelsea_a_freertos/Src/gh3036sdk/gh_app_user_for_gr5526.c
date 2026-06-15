/**
  ****************************************************************************************
  * @file    gh_app_user.c
  * @author  GHealth Driver Team
  * @brief   gh application user file
  ****************************************************************************************
  * @attention
  #####Copyright (c) 2024 GOODIX
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

  ****************************************************************************************
  */

/*
 * INCLUDE FILES
 *****************************************************************************************
 */
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
#if (1 == GH_PROTOCOL_EN)
#include "gh_data_package.h"
#endif
#include "gh_app_manager.h"

#include "app_thread.h"
#include "hrs.h"
#include "factory.h"

#if GH_USER_LOG_EN
#define DEBUG_LOG(...)                      GH_LOG_LVL_DEBUG(__VA_ARGS__)
#define WARNING_LOG(...)                    GH_LOG_LVL_WARNING(__VA_ARGS__)
#define ERROR_LOG(...)                      GH_LOG_LVL_ERROR(__VA_ARGS__)
#else
#define DEBUG_LOG(...)
#define WARNING_LOG(...)
#define ERROR_LOG(...)
#endif

/*
 * DEFINES
 *****************************************************************************************
 */
#define STR_MAX_LEN             256

/*
 * STRUCT DEFINE
 *****************************************************************************************
 */


/*
 * LOCAL FUNCTION DEFINITIONS
 *****************************************************************************************
 */
extern bool is_connect(void);

/*
 * LOCAL VARIABLE DEFINITIONS
 *****************************************************************************************
 */


/*
 * LOCAL FUNCTION DEFINITIONS
 *****************************************************************************************
 */

uint32_t gh_demo_int_process(void)
{
    return gh_hal_isr();
}

uint32_t gh_demo_config_ctrl(gh_hal_config_ctrl_t *p_config_ctrl)
{
    switch (p_config_ctrl->op)
    {
        case GH_HAL_CONFIG_SET:
        {
            gh_exit_lowpower_mode();
            gh_hal_service_cfg_download(p_config_ctrl->p_reg, p_config_ctrl->number);
            gh_enter_lowpower_mode();
        }
        break;

        default:
        break;
    }

    return 0;
}

uint32_t gh_demo_sample_ctrl(gh_hal_sample_ctrl_t *p_sample_ctrl)
{
    switch (p_sample_ctrl->op)
    {
        case GH_HAL_SAMPLE_TOP_STOP:
        {
            gh_hal_top_ctrl(0);
        }
        break;

        case GH_HAL_SAMPLE_TOP_START:
        {
            gh_hal_top_ctrl(1);
        }
        break;

        case GH_HAL_SAMPLE_CHN_EN:
        {
            gh_hal_channel_en(&p_sample_ctrl->ch_map);
        }
        break;

        default:
        break;
    }

    return 0;
}

#if ((1 == GH_DEMO_DATA_LOG_EN) && (1 == GH_LOG_DEBUG_ENABLE))
uint32_t gh_demo_data_log(gh_func_frame_t *p_frame)
{
    char gf_str[STR_MAX_LEN] = {0};
    char raw_str[STR_MAX_LEN] = {0};
    uint32_t flag0 = 0;
    uint32_t flag1 = 0;
    uint16_t offset = 0;
    uint16_t log_len = 0;
    uint16_t remain = sizeof(gf_str);
    for (uint16_t channel_cnt = 0; channel_cnt < p_frame->ch_num; channel_cnt ++)
    {
        //gain & current
        uint64_t agc_info = 0;
        gh_memcpy(&agc_info, &(p_frame->p_data[channel_cnt].agc_info), sizeof(gh_agc_info_t));
        log_len = GH_SNPRINTF(gf_str + offset, remain, "%lld,", agc_info);
        offset += log_len;
        remain -= log_len;
    }
    offset = 0;
    remain = sizeof(raw_str);
    for (uint16_t channel_cnt = 0; channel_cnt < p_frame->ch_num; channel_cnt ++)
    {
        //int32_t rawdata = p_frame->p_data[channel_cnt].rawdata;
        int32_t rawdata = p_frame->p_data[channel_cnt].ipd_pa;
        log_len = GH_SNPRINTF(raw_str + offset, remain, "%d,", rawdata);
        offset += log_len;
        remain -= log_len;
    }
    for (uint16_t channel_cnt = 0; channel_cnt < p_frame->ch_num; channel_cnt ++)
    {
        flag0 |= (p_frame->p_data[channel_cnt].flag.led_adj_flag << channel_cnt);
        flag1 |= (p_frame->p_data[channel_cnt].flag.sa_flag << channel_cnt);
    }
    switch (p_frame->id)
    {
#if (GH_USE_GOODIX_HR_ALGO)
        case GH_FUNC_FIX_IDX_HR:
        {
            DEBUG_LOG("[%d]hr_fi=%d,gs=%d,%d,%d,CF:%d,%d,GF=%sraw:%s\n",
                             (int32_t)(p_frame->timestamp/1000),
                             (int32_t)(p_frame->frame_cnt),
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCX_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCY_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCZ_IDX],
                             (int32_t)flag0,
                             (int32_t)flag1,
                             gf_str,
                             raw_str);
            if (GH_NULL_PTR != p_frame->p_algo_res)
            {
                gh_algo_hr_result_t *p_hr_res = (gh_algo_hr_result_t *)p_frame->p_algo_res;
                if (0 != p_hr_res->hba_out_flag)
                {
                    DEBUG_LOG("[%d]hr_data hr:%d,confi:%d \n",
                                     (int32_t)(p_frame->timestamp/1000),
                                     (int32_t)p_hr_res->hba_out,
                                     (int32_t)p_hr_res->valid_score);
                }
            }
        }
        break;
#endif
#if (GH_USE_GOODIX_SPO2_ALGO)
        case GH_FUNC_FIX_IDX_SPO2:
        {
            DEBUG_LOG("[%d]spo2_fi=%d,gs=%d,%d,%d,CF:%d,%d,GF=%sraw:%s\n",
                             (int32_t)(p_frame->timestamp/1000),
                             (int32_t)(p_frame->frame_cnt),
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCX_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCY_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCZ_IDX],
                             (int32_t)flag0,
                             (int32_t)flag1,
                             gf_str,
                             raw_str);
            if (GH_NULL_PTR != p_frame->p_algo_res)
            {
                gh_algo_spo2_result_t *p_spo2_res = (gh_algo_spo2_result_t *)p_frame->p_algo_res;
                if (0 != p_spo2_res->final_calc_flg)
                {
                    DEBUG_LOG("[%d]spo2_data spo2:%d,R:%d,valid:%d \n",
                                     (int32_t)(p_frame->timestamp/1000),
                                     (int32_t)p_spo2_res->final_spo2 / GH_ALGO_SPO2_OUT_COEF,
                                     (int32_t)p_spo2_res->final_r_val,
                                     (int32_t)p_spo2_res->final_valid_level);
                }
            }
        }
        break;
#endif
#if (GH_USE_GOODIX_HRV_ALGO)
        case GH_FUNC_FIX_IDX_HRV:
        {
            DEBUG_LOG("[%d]hrv_fi=%d,gs=%d,%d,%d,CF:%d,%d,GF=%sraw:%s\n",
                             (int32_t)(p_frame->timestamp/1000),
                             (int32_t)(p_frame->frame_cnt),
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCX_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCY_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCZ_IDX],
                             (int32_t)flag0,
                             (int32_t)flag1,
                             gf_str,
                             raw_str);
            if (GH_NULL_PTR != p_frame->p_algo_res)
            {
                if (gh_has_nonzero(p_frame->p_algo_res, sizeof(goodix_hrv_result)))
                {
                    gh_algo_hrv_result_t *p_hrv_res = (gh_algo_hrv_result_t *)p_frame->p_algo_res;
                    DEBUG_LOG("[%d]hrv rri0 = %d, rri1 = %d, conf = %d, num = %d\r\n",
                                     (int32_t)(p_frame->timestamp/1000),
                                     (int32_t)p_hrv_res->rri[0], (int32_t)p_hrv_res->rri[1],
                                     p_hrv_res->rri_confidence,  p_hrv_res->rri_valid_num);
                }
            }
        }
        break;
#endif
#if (GH_USE_GOODIX_NADT_ALGO)
        case GH_FUNC_FIX_IDX_GNADT:
        {
            DEBUG_LOG("[%d]NG_fi=%d,gs=%d,%d,%d,CF:%d,%d,GF=%sraw:%s\n",
                             (int32_t)(p_frame->timestamp/1000),
                             (int32_t)(p_frame->frame_cnt),
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCX_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCY_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCZ_IDX],
                             (int32_t)flag0,
                             (int32_t)flag1,
                             gf_str,
                             raw_str);
                if (GH_NULL_PTR != p_frame->p_algo_res)
                {
                    gh_algo_nadt_result_t *p_nadt_res = (gh_algo_nadt_result_t *)p_frame->p_algo_res;

                    if (p_nadt_res->nadt_out_flag)
                    {
                        DEBUG_LOG("[%d]NG_data r0:%d,r1:%d\n",
                                         (int32_t)(p_frame->timestamp/1000),
                                         (int32_t)p_nadt_res->nadt_out,
                                         (int32_t)p_nadt_res->nadt_confi);
                    }
                }
        }
        break;

        case GH_FUNC_FIX_IDX_IRNADT:
        {
            DEBUG_LOG("[%d]NI_fi=%d,gs=%d,%d,%d,CF:%d,%d,GF=%sraw:%s\n",
                             (int32_t)(p_frame->timestamp/1000),
                             (int32_t)(p_frame->frame_cnt),
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCX_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCY_IDX],
                             (int32_t)p_frame->gsensor_data.acc[GH_ACCZ_IDX],
                             (int32_t)flag0,
                             (int32_t)flag1,
                             gf_str,
                             raw_str);
                if (GH_NULL_PTR != p_frame->p_algo_res)
                {
                    gh_algo_nadt_result_t *p_nadt_res = (gh_algo_nadt_result_t *)p_frame->p_algo_res;

                    if (p_nadt_res->nadt_out_flag)
                    {
                        DEBUG_LOG("[%d]NI_data r0:%d,r1:%d\n",
                                         (int32_t)(p_frame->timestamp/1000),
                                         (int32_t)p_nadt_res->nadt_out,
                                         (int32_t)p_nadt_res->nadt_confi);
                    }
                }

        }
        break;
#endif
        default:
        break;
    } // switch (p_frame->id)
    return 0;
}
#endif

uint32_t gh_demo_data_publish(gh_func_frame_t *p_frame)
{
#if (1 == GH_PROTOCOL_EN)
    if (is_connect()) {
        gh_protocol_process(p_frame);
    }
#endif
#if ((1 == GH_DEMO_DATA_LOG_EN) && (1 == GH_LOG_DEBUG_ENABLE))
    gh_demo_data_log(p_frame);
#endif
    factory_data_process(p_frame);
    /* Example1: get algorithm result */
#if 1
    if ((GH_NULL_PTR == p_frame->p_algo_res)  && 
        ((p_frame->id < GH_FUNC_FIX_IDX_ALGO_MAX)))
    {
        return 0;
    }

    switch (p_frame->id)
    {
        case GH_FUNC_FIX_IDX_ADT:
        {
//           if (p_frame->p_algo_res != NULL) {
//               gh_algo_adt_result_t *p_adt_res = (gh_algo_adt_result_t *)p_frame->p_algo_res;
//               GH_LOG_LVL_DEBUG("%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
//                       p_frame->timestamp,
//                       p_frame->frame_cnt,
//                       p_frame->gsensor_data.acc[0], 
//                       p_frame->gsensor_data.acc[1], 
//                       p_frame->gsensor_data.acc[2],
//                       p_frame->p_data[0].rawdata, 
//                       p_frame->p_data[1].rawdata,
//                       p_frame->p_data[0].ipd_pa, 
//                       p_frame->p_data[1].ipd_pa,
//                       (uint32_t)(p_frame->p_data[0].agc_info), 
//                       (uint32_t)(p_frame->p_data[1].agc_info),
//                       p_adt_res->wear_evt,
//                       p_adt_res->det_status,
//                       p_adt_res->ctr,
//                       p_adt_res->update,
//                       );
//           } else {
//               GH_LOG_LVL_DEBUG("%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
//                       p_frame->timestamp,
//                       p_frame->frame_cnt,
//                       p_frame->gsensor_data.acc[0], 
//                       p_frame->gsensor_data.acc[1], 
//                       p_frame->gsensor_data.acc[2],
//                       p_frame->p_data[0].rawdata, 
//                       p_frame->p_data[1].rawdata,
//                       p_frame->p_data[0].ipd_pa, 
//                       p_frame->p_data[1].ipd_pa,
//                       (uint32_t)(p_frame->p_data[0].agc_info), 
//                       (uint32_t)(p_frame->p_data[1].agc_info)
//               );
//           }

        }
        break;

        case GH_FUNC_FIX_IDX_HR:
        {
            /* HR algorithm result example */
//            gh_algo_hr_result_t *p_hr_res = (gh_algo_hr_result_t *)p_frame->p_algo_res;
//            DEBUG_LOG("[HR] flag:%d, out:%d, snr:%d, level:%d, score:%d, acc:%d, scence:%d\r\n",
//                          p_hr_res->hba_out_flag, p_hr_res->hba_out, p_hr_res->hba_snr,
//                          p_hr_res->valid_level, p_hr_res->valid_score, p_hr_res->hba_acc_info,
//                          p_hr_res->hba_reg_scence);
            gh_algo_hr_result_t *p_hr_res = (gh_algo_hr_result_t *)p_frame->p_algo_res;
            if ((p_hr_res != NULL) && (p_hr_res->hba_out_flag)) {
                hrs_heart_rate_measurement_send(0, p_hr_res->hba_out, false);
            }
        }
        break;

        case GH_FUNC_FIX_IDX_SPO2:
        {
            /* SPO2 algorithm result example */
//            gh_algo_spo2_result_t *p_spo2_res = (gh_algo_spo2_result_t *)p_frame->p_algo_res;
//            DEBUG_LOG("[SPO2] final:%d, r:%d, confi:%d, level:%d, hb:%d, flag:%d\r\n",
//                          p_spo2_res->final_spo2, p_spo2_res->r_val, p_spo2_res->confi_coeff,
//                          p_spo2_res->valid_level, p_spo2_res->hb_mean, p_spo2_res->invalid_flag);
//            if (p_frame->p_algo_res != NULL) {
//                gh_algo_spo2_result_t *p_spo2_res = (gh_algo_spo2_result_t *)p_frame->p_algo_res;
//                GH_LOG_LVL_DEBUG("%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
//                        p_frame->timestamp,
//                        p_frame->frame_cnt,
//                        p_frame->gsensor_data.acc[0], 
//                        p_frame->gsensor_data.acc[1], 
//                        p_frame->gsensor_data.acc[2],
//                        p_frame->p_data[0].rawdata, 
//                        p_frame->p_data[1].rawdata,
//                        p_frame->p_data[0].ipd_pa, 
//                        p_frame->p_data[1].ipd_pa,
//                        (uint32_t)(p_frame->p_data[0].agc_info), 
//                        (uint32_t)(p_frame->p_data[1].agc_info),
//                        p_spo2_res->final_spo2,
//                        p_spo2_res->final_r_val,
//                        p_spo2_res->final_valid_level,
//                        p_spo2_res->final_confi_coeff,
//                        p_spo2_res->final_hb_mean,
//                        p_spo2_res->final_hb_confi_lvl,
//                        p_spo2_res->final_invalidFlg,
//                        p_spo2_res->final_calc_flg
//                        );
//            } else {
//                GH_LOG_LVL_DEBUG("%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\r\n",
//                        p_frame->timestamp,
//                        p_frame->frame_cnt,
//                        p_frame->gsensor_data.acc[0], 
//                        p_frame->gsensor_data.acc[1], 
//                        p_frame->gsensor_data.acc[2],
//                        p_frame->p_data[0].rawdata, 
//                        p_frame->p_data[1].rawdata,
//                        p_frame->p_data[0].ipd_pa, 
//                        p_frame->p_data[1].ipd_pa,
//                        (uint32_t)(p_frame->p_data[0].agc_info), 
//                        (uint32_t)(p_frame->p_data[1].agc_info)
//                );
//            }

        }
        break;

        case GH_FUNC_FIX_IDX_HRV:
        {
            /* HRV algorithm result example */
//            gh_algo_hrv_result_t *p_hrv_res = (gh_algo_hrv_result_t *)p_frame->p_algo_res;
//            DEBUG_LOG("[HRV] rri0:%d, rri1:%d, rri2:%d, rri3:%d, confi:%d, num:%d\r\n",
//                          p_hrv_res->rri[0], p_hrv_res->rri[1], p_hrv_res->rri[2],
//                          p_hrv_res->rri[3], p_hrv_res->confidence, p_hrv_res->valid_num);
            gh_algo_hrv_result_t *p_hrv_res = (gh_algo_hrv_result_t *)p_frame->p_algo_res;
            if ((p_hrv_res != NULL) && ((p_frame->frame_cnt % 25) == 0)) {
                for (uint8_t i = 0; i < p_hrv_res->rri_valid_num; i++) {
                    hrs_rr_interval_add(p_hrv_res->rri[i]);
                }
            }
        }
        break;

        case GH_FUNC_FIX_IDX_GNADT:
        case GH_FUNC_FIX_IDX_IRNADT:
        {
            /* NADT algorithm result example */
            gh_algo_nadt_result_t *p_nadt_res = (gh_algo_nadt_result_t *)p_frame->p_algo_res;
            if (p_frame->frame_cnt % 25 == 0) {
            
                    DEBUG_LOG("[NADT] wear_off_detect:%d, live_body_conf:%d\r\n",
                          GH_ALGO_NADT_RES0_MASK(p_nadt_res->nadt_out), p_nadt_res->nadt_confi);
                }
        }
        break;

        default:
        break;
    } // switch (p_frame->id)
#endif


    return 0;
}

#if (GH_MTSS_EN)
uint32_t gh_demo_mtss_event_publish(gh_action_event_e evt, uint64_t timestamp)
{
    switch (evt)
    {
        case GH_ACTION_WEAR_ON:
        {
            DEBUG_LOG("GH_ACTION_WEAR_ON");
            health_wear_update_event_send(WEAR_EVENT_ON_ADT);
        }
        break;

        case GH_ACTION_WEAR_OFF:
        {
            DEBUG_LOG("GH_ACTION_WEAR_OFF");
            health_wear_update_event_send(WEAR_EVENT_OFF_ADT);
        }
        break;

        case GH_ACTION_MOVE:
        {
            DEBUG_LOG("GH_ACTION_MOVE");
            health_start_event_send(HEALTH_MODE_ADT, false);
            // example: gsensor detects motion, add code about start Ghealth ADT sample
#if ((GH_FUSION_MODE_SEL) == (GH_FUSION_MODE_SYNC))
            gh_move_det_timer_stop();
#endif
        }
        break;

        case GH_ACTION_MOVE_TIME_OUT:
        {
            DEBUG_LOG("GH_ACTION_MOVE_TIME_OUT");
            health_stop_event_send(HEALTH_MODE_ADT, false);
            // example: gsensor detects static, add code about stop Ghealth ADT sample
#if ((GH_FUSION_MODE_SEL) == (GH_FUSION_MODE_SYNC))
            gh_move_det_timer_start();
#endif
        }
        break;

        default:
        break;
    }

    return 0;
}
#else

uint32_t gh_demo_action_event_publish(gh_action_event_e evt, uint64_t timestamp)
{
    switch (evt)
    {
        case GH_ACTION_ADT_WEAR_ON:
        {
            // example: add customer handle after ADT wear on
            health_wear_update_event_send(WEAR_EVENT_ON_ADT);
        }
        break;

        case GH_ACTION_ADT_WEAR_OFF:
        {
            // example: add customer handle after ADT wear off
            health_wear_update_event_send(WEAR_EVENT_OFF_ADT);
        }
        break;

        case GH_ACTION_NADT_WEAR_ON:
        {
            // example: add customer handle after NADT wear on
            health_wear_update_event_send(WEAR_EVENT_ON_NADT);
        }
        break;

        case GH_ACTION_NADT_WEAR_OFF:
        {
            // example: add customer handle after NADT wear off(non-living)
            health_wear_update_event_send(WEAR_EVENT_OFF_NADT);
        }
        break;

        case GH_ACTION_MOVE:
        {
            // example: gsensor detects motion, add code about start Ghealth ADT sample
#if ((GH_FUSION_MODE_SEL) == (GH_FUSION_MODE_SYNC))
            gh_move_det_timer_stop();
#endif
        }
        break;

        case GH_ACTION_MOVE_TIME_OUT:
        {
            // example: gsensor detects static, add code about stop Ghealth ADT sample
#if ((GH_FUSION_MODE_SEL) == (GH_FUSION_MODE_SYNC))
            gh_move_det_timer_start();
#endif
        }
        break;

        default:
        break;
    } // switch (evt)

    return 0;
}
#endif

#if (1 == GH_APP_MIPS_STA_EN)
uint64_t gh_demo_app_mips_ts_get(void)
{
    return 0;
}
#endif

#if ((GH_FUSION_MODE_SEL) == (GH_FUSION_MODE_SYNC))
uint32_t gh_move_det_timer_start(void)
{
    //bsp_gs_timer_start();
    return 0;
}

uint32_t gh_move_det_timer_stop(void)
{
    //bsp_gs_timer_stop();
    return 0;
}
#endif
