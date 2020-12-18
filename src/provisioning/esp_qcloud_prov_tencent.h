// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

typedef enum {
    CMD_TOKEN_ONLY 			= 0,        /* Token only for smartconfig  */
    CMD_SSID_PW_TOKEN 		= 1,        /* SSID/PW/TOKEN for softAP */
    CMD_DEVICE_REPLY 		= 2,       	/* device reply */
    CMD_BLE_TOKEN 			= 3,        /* device ble token */
    CMD_BLE_LOG_UPLOAD 		= 4,        /* device ble log upload */
} type_wifi_config_t;

esp_err_t esp_qcloud_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                       uint8_t **outbuf, ssize_t *outlen, void *priv_data);

/**
 * @brief Open UDP service to communicate with WeChat applet.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_udp_server_start(void);

/**
 * @brief Stop UDP service.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_udp_server_stop(void);

/**
 * @brief Set token val and notify start connecting to the QCloud
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_prov_ble_set_token(char *token, size_t token_len);

/**
 * @brief Get ble connection status
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail 
 */
bool esp_qcloud_prov_ble_is_connected(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
