/*****************************************************************************
 * Copyright (c) 2019, Nations Technologies Inc.
 *
 * All rights reserved.
 * ****************************************************************************
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the disclaimer below.
 *
 * Nations' name may not be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * DISCLAIMER: THIS SOFTWARE IS PROVIDED BY NATIONS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * DISCLAIMED. IN NO EVENT SHALL NATIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ****************************************************************************/

/**
 * @brief bluetooth functions.
 * @file n32wb452_ble_api.h
 * @author Nations
 * @version v1.0.1
 * @copyright Copyright (c) 2019, Nations Technologies Inc. All rights reserved.
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __N32WB452_BLE_API_H__
#define __N32WB452_BLE_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "n32wb452.h"

#define BLE_SERVICE_USED        1
#define BLE_CHARACTER_USED      1

/**
 * @brief return enumeration
 */
typedef enum bt_ret_enum_t
{
    BT_RET_SUCCESS = 0,
    BT_RET_OPERATED_FAILDED_ERR,
    BT_RET_PARAM_NULL_ERR,
    BT_RET_PARAM_ERR,
}bt_ret_enum;


/**
 * @brief bluetooth event enumeration
 */
typedef enum bt_event_enum_t
{
    BT_EVENT_VERSION,
    BT_EVENT_CONNECTED,
    BT_EVENT_DISCONNECTD,
    BT_EVENT_RCV_DATA,
}bt_event_enum;

/**
 * @brief character permission enumeration 
 */
typedef enum bt_character_perm_t
{
    BT_RD_PERM          = 0x00001,
    BT_WRITE_PERM       = 0x00002,
    BT_WRITE_REQ_PERM   = 0x00004,
    BT_NTF_PERM         = 0x00008,
}bt_character_perm;


typedef void (*bt_event_callback_handler_t)(bt_event_enum event, const uint8_t * data, uint32_t size, uint32_t character_uuid);


/**
 * @brief bluetooth control parameter
 */
typedef struct bt_control_param_t
{
    uint32_t multi_mode:1;  //0:disable connect multiple,1:enable connect multiple
    uint32_t reserved:31;  //
}bt_control_param;

/**
 * @brief bluetooth service parameter
 */
typedef struct bt_svc_param_t
{
    uint16_t svc_uuid;  /// 16 bits UUID, start from 0xFEC1

    struct bt_character{
        uint16_t uuid;
        uint16_t permission;//reference to bt_character_perm
    }character[BLE_CHARACTER_USED];
}bt_svc_param;



/**
 * @brief bluetooth attribute parameter configuration
 */
typedef struct bt_attr_param_t
{
    bt_control_param ctrl_param;    //bluetooth control parameter 
    uint8_t device_name[32];        //bluetooth device name. It is valid for more than 4 characters. If all are 0x00 or invalid, use the default name 
    uint8_t scan_rsp_data[31];      //broadcast response data, the vatual valid length is 30
    uint8_t scan_rsp_data_len;      //broadcast response data length
    uint8_t device_addr[20];        //format is: "11:22:33:44:55:66". if all are 0x00, use the defalut address

    //adv data
    bt_svc_param service[BLE_SERVICE_USED];
}bt_attr_param;


/***************************************************************************************
declaraction: int32_t bt_ware_init(bt_event_pcallback pcallback)
function    : Initialize bluetooth module
parameter   : bluetooth initialization parameters
parameter   : bluetooth callback evnet
return      : NONE
***************************************************************************************/
int32_t bt_ware_init(bt_attr_param *pinit, bt_event_callback_handler_t pcallback);

/***************************************************************************************
declaraction: void bt_handler(void)
function    : bluetooth interrupt handler
parameter   : void
return      : NONE
***************************************************************************************/
void bt_handler(void);

/***************************************************************************************
declaraction: void bt_run_thread(void)
function    : bluetooth running main thread
parameter   : void
return      : NONE
***************************************************************************************/
void bt_run_thread(void);


/***************************************************************************************
declaraction: uint32_t bt_rcv_data(uint8_t *data, uint32_t size, uint32_t character)
function    : According to BT_EVENT_RCV_DATA event, read data from corresponding character
parameter   : uint8_t *data receive data buf
parameter   : uint32_t size buf size(No more than 512Bytes)
parameter   : uint32_t character character ID
return      : return read size
***************************************************************************************/
uint32_t bt_rcv_data(uint8_t *data, uint32_t size, uint32_t character);


/***************************************************************************************
declaraction: uint32_t bt_snd_data(const uint8_t *data, uint32_t size, uint32_t character)
function    : bluetooth send data to corresponding character
parameter   : uint8_t *data send data buf
parameter   : uint32_t send data size (No more than 512Bytes)
parameter   : uint32_t character character ID
return      : return 0
***************************************************************************************/
uint32_t bt_snd_data(const uint8_t *data, uint32_t size, uint32_t character);


/***************************************************************************************
declaraction: void bt_disconnect(void)
function    : slave actively disconnects the bluetooth connection
parameter   : NULL
return      : NULL
***************************************************************************************/
void bt_disconnect(void);

/***************************************************************************************
declaraction: void ble_status_monitor(void)
function    : bluetooth status monitor
parameter   : NULL
return      : NULL
***************************************************************************************/
void ble_status_monitor(void);

/*==============================================================================
BT_Init
<Description>
    This function is to initialize bluetooth stack
==============================================================================*/
void BT_init(void);


/*==============================================================================
is_bt_busy

<Description>
    This function is to get the bt kernel processing is ongoing or not. 
    if not onging, then allow to sleep, otherwise not allow to sleep.

<return>
    0:  bt kernel processing is not ongoing
    1:  bt kernel processing is ongoing
==============================================================================*/
bool is_bt_busy(void);


/*==============================================================================
BT_handle

<Description>
    This function is schedule the BT tasks
==============================================================================*/
void BT_handle(void);

/*==============================================================================
BT_get_version

<Description>
    This function is to get version  of BT API. 

<parameter out>
    version:   store the data of the BT API version, size 10 byte;
==============================================================================*/
void BT_get_version(uint8_t * version);

extern bt_event_callback_handler_t g_pcallback;
extern uint32_t g_connnet_start;

/*==============================================================================
ble_get_drv_version


<Description>
    This function is to get the version of the ble driver. 

<parameter out>
    version:   store the data of the ble driver version, size 6 byte:"V1.0.0";
==============================================================================*/
extern void ble_get_drv_version(char *version);

#if defined __cplusplus
}
#endif
#endif // __N32WB452_BLE_API_H__
