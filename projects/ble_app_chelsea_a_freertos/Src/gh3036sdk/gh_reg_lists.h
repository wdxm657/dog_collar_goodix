/**
 ****************************************************************************************
 *
 * @file    gh_reg_lists.h
 * @author  GOODIX GH Driver Team
 * @brief   Header file containing project reg lists.
 *
 ****************************************************************************************
**/

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __GH_REG_CONFIG_H__
#define __GH_REG_CONFIG_H__

#include <stdint.h>
#include "gh_public_api.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    const gh_config_reg_t* reg_cfg;
    uint16_t reg_cfg_len;
} gh_config_list_t;

/**
 * @brief default registers map
 */
extern const gh_config_list_t g_reg_lists[];

/**
 * @brief default registers map size
 */
extern const uint16_t g_reg_lists_max_size;

#ifdef __cplusplus
}
#endif

#endif /* __GH_REG_CONFIG_H__ */

