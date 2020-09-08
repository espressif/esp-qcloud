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

/**< ESP QCloud Event Base */
ESP_EVENT_DECLARE_BASE(QCLOUD_EVENT);

/**< ESP QCloud Events */
typedef enum {
    QCLOUD_EVENT_INIT_DONE = 1,    /**< QCloud Core Initialisation Done */
    QCLOUD_EVENT_DEBUG_FLASH_FULL,
} esp_qcloud_event_t;


typedef enum {
    QCLOUD_AUTH_MODE_INVALID,
    QCLOUD_AUTH_MODE_KEY,
    QCLOUD_AUTH_MODE_CERT,
    QCLOUD_AUTH_MODE_DYNREG,
} esp_qcloud_auth_mode_t;


/**< ESP QCloud Parameter Value type */
typedef enum {
    QCLOUD_VAL_TYPE_INVALID = 0, /**< Invalid */
    QCLOUD_VAL_TYPE_BOOLEAN,     /**< Boolean */
    QCLOUD_VAL_TYPE_INTEGER,     /**< Integer. Mapped to a 32 bit signed integer */
    QCLOUD_VAL_TYPE_FLOAT,       /**< Floating point number */
    QCLOUD_VAL_TYPE_STRING,      /**< NULL terminated string */
} esp_qcloud_param_val_type_t;

/**< ESP QCloud Value */
typedef struct {
    esp_qcloud_param_val_type_t type;
    float f; /**< Float */
    union {
        bool b;  /**< Boolean */
        int i;   /**< Integer */
        char *s; /**< NULL terminated string */
    };
} esp_qcloud_param_val_t;

typedef esp_err_t (*esp_qcloud_get_param_t)(const char *id, esp_qcloud_param_val_t *val);
typedef esp_err_t (*esp_qcloud_set_param_t)(const char *id, const esp_qcloud_param_val_t *val);

esp_err_t esp_qcloud_device_handle(const esp_qcloud_get_param_t get_param_cb,
                                   const esp_qcloud_set_param_t set_param_cb);

esp_err_t esp_qcloud_create_device(const char *product_id, const char *devic_name);
esp_err_t esp_qcloud_device_version(const char *version);
esp_err_t esp_qcloud_device_secret(const char *device_secret);
esp_err_t esp_qcloud_device_cert(const char *cert_crt, const char *private_key);
const char *esp_qcloud_get_version();
const char *esp_qcloud_get_device_name();
const char *esp_qcloud_get_product_id();
esp_qcloud_auth_mode_t esp_qcloud_get_auth_mode();
const char *esp_qcloud_get_device_secret();
const char *esp_qcloud_get_cert_crt();
const char *esp_qcloud_get_private_key();



esp_qcloud_param_val_t esp_qcloud_bool(bool val);
esp_qcloud_param_val_t esp_qcloud_int(int val);
esp_qcloud_param_val_t esp_qcloud_float(float val);
esp_qcloud_param_val_t esp_qcloud_str(const char *val);
esp_err_t esp_qcloud_device_param(const char *id, esp_qcloud_param_val_t default_value);

esp_err_t esp_qcloud_handle_set_param(const cJSON *request_params, cJSON *reply_data);
esp_err_t esp_qcloud_handle_get_param(const cJSON *request_data, cJSON *reply_data);

esp_err_t esp_qcloud_iothub_init();
esp_err_t esp_qcloud_iothub_start();
esp_err_t esp_qcloud_iothub_stop();
esp_err_t esp_qcloud_iothub_bind(const char *token);


bool esp_qcloud_iothub_is_connected();

/** Enable OTA
 *
 * Calling this API enables OTA as per the ESP QCloud specification.
 * Please check the various ESP QCloud configuration options to
 * use the different variants of OTA. Refer the documentation for
 * additional details.
 *
 * @param[in] ota_config Pointer to an OTA configuration structure
 * @param[in] type The OTA workflow type
 *
 * @return ESP_OK on success
 * @return error on failure
 */
esp_err_t esp_qcloud_iothub_ota_enable();

#ifdef __cplusplus
}
#endif
