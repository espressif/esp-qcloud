// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include "esp_smartconfig.h"

/**
 * @brief softconfig type
 * 
 */
typedef enum {
    SOFTAPCONFIG_TYPE_ESPRESSIF = 1,       /**< protocol: Espressif */
    SOFTAPCONFIG_TYPE_TENCENT,             /**< protocol: Tencent */
    SOFTAPCONFIG_TYPE_ESPRESSIF_TENCENT,   /**< protocol: Espressif and Tencent */
} softapconfig_type_t;

typedef enum {
    BLECONFIG_TYPE_ESPRESSIF = 1,       /**< protocol: Espressif */
    BLECONFIG_TYPE_TENCENT,             /**< protocol: Tencent */
    BLECONFIG_TYPE_ESPRESSIF_TENCENT,   /**< protocol: Espressif and Tencent */
} bleconfig_type_t;

/**
 * @brief Initialize wifi and register events.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_wifi_init(void);

/**
 * @brief Use sta_cfg parameters to start wifi.
 * 
 * @param[in] sta_cfg sta mode configuration parameters.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_wifi_start(const wifi_config_t *sta_cfg);

/**
 * @brief Reset wifi
 * 
 * @return esp_err_t 
 */
esp_err_t esp_qcloud_wifi_reset(void);

/**
 * @brief Start SmartConfig.
 * 
 * @param[in] type Choose from the smartconfig_type_t.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_smartconfig_start(smartconfig_type_t type);

/**
 * @brief Stop SmartConfig.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_smartconfig_stop(void);

/**
 * @brief Start softap.
 * 
 * @param[in] type Softap type, Espressif or Tencent.
 * @param[in] ssid SSID of the softap mode.
 * @param[in] password PassWord of the softap mode.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_softapconfig_start(softapconfig_type_t type, const char *ssid, const char *password);

/**
 * @brief Stop softap.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_softapconfig_stop(void);

/**
 * @brief 
 * 
 * @return esp_err_t 
 */
esp_err_t esp_qcloud_prov_bleconfig_start(bleconfig_type_t type, const char *local_name);

/**
 * @brief 
 * 
 * @return esp_err_t 
 */
esp_err_t esp_qcloud_prov_ble_stop(void);

/**
 * @brief Wait for the provisioning result.
 * 
 * @param[in/out] sta_cfg Save SSID and password.
 * @param[in] wait_ms Time to wait before timeout, in ms.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_wait(wifi_config_t *sta_cfg, uint32_t wait_ms);

/**
 * @brief Print the QR code in softap provisioning mode.
 * 
 * @param[in] name SSID of the softap mode.
 * @param[in] transport 
 */
void esp_qcloud_prov_print_wechat_qr(const char *name, const char *transport);

/**
 * @brief Report binding status
 * 
 * @param token_status 
 * @return esp_err_t 
 */
esp_err_t esp_qcloud_prov_ble_report_bind_status(bool token_status);

#ifdef __cplusplus
}
#endif
