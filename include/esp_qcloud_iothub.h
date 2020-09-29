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

#include <stdint.h>
#include <stdbool.h>

#include <esp_err.h>
#include <esp_event.h>

#include "esp_qcloud_mem.h"
#include "esp_qcloud_utils.h"

#include "cJSON.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief QCloud Length of configuration information.
 */
#define CLIENT_ID_MAX_SIZE               (80)  /**< MAX size of client ID */
#define PRODUCT_ID_SIZE                  (10)  /**< MAX size of product ID */
#define PRODUCT_SECRET_MAX_SIZE          (32)  /**< MAX size of product secret */
#define DEVICE_NAME_MAX_SIZE             (48)  /**< MAX size of device name */
#define DEVICE_SECRET_SIZE               (24)  /**< MAX size of device secret */
#define DEVICE_CERT_FILE_NAME_MAX_SIZE   (128) /**< MAX size of device cert file name */

#define AUTH_TOKEN_MAX_SIZE              (32)  /**< MAX size of auth token */

/**
 * @brief ESP QCloud Event Base.
 */
ESP_EVENT_DECLARE_BASE(QCLOUD_EVENT);

/**
 * @brief ESP QCloud Events.
 */
typedef enum {
    QCLOUD_EVENT_IOTHUB_INIT_DONE = 1,    /**< QCloud core Initialisation Done */
    QCLOUD_EVENT_IOTHUB_BOND_DEVICE,      /**< QCloud bind device */
    QCLOUD_EVENT_IOTHUB_UNBOND_DEVICE,    /**< QCloud unbind device */
    QCLOUD_EVENT_LOG_FLASH_FULL,          /**< QCloud log storage full */
} esp_qcloud_event_t;

/**
 * @brief ESP QCloud Auth mode.
 */
typedef enum {
    QCLOUD_AUTH_MODE_INVALID,       /**< Invalid mode */
    QCLOUD_AUTH_MODE_KEY,           /**< Key authentication */
    QCLOUD_AUTH_MODE_CERT,          /**< Certificate authentication */ 
    QCLOUD_AUTH_MODE_DYNREG,        /**< Dynamic authentication */
} esp_qcloud_auth_mode_t;

/**
 * @brief ESP QCloud Parameter Value type.
 */
typedef enum {
    QCLOUD_VAL_TYPE_INVALID = 0, /**< Invalid */
    QCLOUD_VAL_TYPE_BOOLEAN,     /**< Boolean */
    QCLOUD_VAL_TYPE_INTEGER,     /**< Integer. Mapped to a 32 bit signed integer */
    QCLOUD_VAL_TYPE_FLOAT,       /**< Floating point number */
    QCLOUD_VAL_TYPE_STRING,      /**< NULL terminated string */
} esp_qcloud_param_val_type_t;

/**
 * @brief ESP QCloud Value.
 */
typedef struct {
    esp_qcloud_param_val_type_t type;
    float f;     /**< Float */
    union {
        bool b;  /**< Boolean */
        int i;   /**< Integer */
        char *s; /**< NULL terminated string */
    };
} esp_qcloud_param_val_t;

/**
 * @brief Interface method get_param.
 * 
 */
typedef esp_err_t (*esp_qcloud_get_param_t)(const char *id, esp_qcloud_param_val_t *val);

/**
 * @brief Interface method set_param.
 * 
 */
typedef esp_err_t (*esp_qcloud_set_param_t)(const char *id, const esp_qcloud_param_val_t *val);

/**
 * @brief Add device callback function, set and get.
 * 
 * @param[in] get_param_cb Get param interface.
 * @param[in] set_param_cb Set param interface.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_device_add_cb(const esp_qcloud_get_param_t get_param_cb,
                                   const esp_qcloud_set_param_t set_param_cb);

/**
 * @brief Create device.
 * 
 * @note Need product id, device name, device key.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_create_device(void);

/**
 * @brief Add firmware version information.
 * 
 * @param[in] version Current firmware version.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_device_add_fw_version(const char *version);

/**
 * @brief Get firmware version information.
 * 
 * @return Pointer to firmware version.
 */
const char *esp_qcloud_get_version(void);

/**
 * @brief Get device name.
 * 
 * @return Pointer to device name.
 */
const char *esp_qcloud_get_device_name(void);

/**
 * @brief Get product id.
 * 
 * @return Pointer to product id.
 */
const char *esp_qcloud_get_product_id(void);

/**
 * @brief Get authentication mode.
 * 
 * @return Current authentication model.
 */
esp_qcloud_auth_mode_t esp_qcloud_get_auth_mode(void);

/**
 * @brief Get device secret.
 * 
 * @return Pointer to device secret.
 */
const char *esp_qcloud_get_device_secret(void);

/**
 * @brief Get Certification.
 * 
 * @return Pointer to certification.
 */
const char *esp_qcloud_get_cert_crt(void);

/**
 * @brief Get private key.
 * 
 * @return Pointer to private key.
 */
const char *esp_qcloud_get_private_key(void);

/**
 * @brief Add properties to your device
 * 
 * @note You need to register these properties on Qcloud, Ensure property identifier is correct.
 * 
 * @param[in] id property identifier.
 * @param[in] type property type.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_device_add_param(const char *id, esp_qcloud_param_val_type_t type);

/**
 * @brief Set local properties.
 * 
 * @note When a control message is received, the function will be called.
 * This is an internal function, You can not modify it.
 * You need to pass your function through esp_qcloud_device_add_cb.
 * 
 * @param[in] request_params 
 * @param[in] reply_data 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_handle_set_param(const cJSON *request_params, cJSON *reply_data);

/**
 * @brief Get local properties.
 * 
 * @note When a control message is received, the function will be called.
 * This is an internal function, You can not modify it.
 * You need to pass your function through esp_qcloud_device_add_cb.
 * 
 * @param[in] request_data 
 * @param[in] reply_data 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_handle_get_param(const cJSON *request_data, cJSON *reply_data);

/**
 * @brief Initialize Qcloud and establish MQTT service.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_iothub_init(void);

/**
 * @brief Run Qcloud service and register related parameters.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_iothub_start(void);

/**
 * @brief Stop Qcloud service and release related resources.
 * 
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_iothub_stop(void);

/**
 * @brief Use token to bind with Qcloud.
 * 
 * @param[in] token Token comes from WeChat applet.
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_iothub_bind(const char *token);

/**
 * @brief Get Qcloud service status.
 * 
 * @return true Connect
 * @return false Disconnect
 */
bool esp_qcloud_iothub_is_connected(void);

/** 
 * @brief Enable OTA
 *
 * @note Calling this API enables OTA as per the ESP QCloud specification.
 * Please check the various ESP QCloud configuration options to
 * use the different variants of OTA. Refer the documentation for
 * additional details.
 *
 * @return
 *     - ESP_OK: succeed
 *     - others: fail
 */
esp_err_t esp_qcloud_iothub_ota_enable(void);

#ifdef __cplusplus
}
#endif
