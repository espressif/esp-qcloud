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

#include <string.h>

#include <esp_log.h>

#include "esp_qcloud_iothub.h"
#include "esp_qcloud_utils.h"

#ifdef CONFIG_QCLOUD_MASS_MANUFACTURE
#include "nvs.h"
#include "nvs_flash.h"
#endif

/* Handle to maintain internal information (will move to an internal file) */
typedef struct {
    char *product_id;
    char *version;
    esp_qcloud_auth_mode_t auth_mode;
    char *device_name;
    union {
        char *device_secret;
        struct  {
            char *cert_crt;
            char *private_key;
        };
    };
} esp_qcloud_profile_t;

static esp_qcloud_profile_t *g_device_profile = NULL;
static esp_qcloud_set_param_t g_esp_qcloud_set_param = NULL;
static esp_qcloud_get_param_t g_esp_qcloud_get_param = NULL;

static SLIST_HEAD(param_list_, esp_qcloud_param) g_property_list;
static SLIST_HEAD(action_list_, esp_qcloud_action) g_action_list;

#ifdef CONFIG_AUTH_MODE_CERT
extern const uint8_t dev_cert_crt_start[] asm("_binary_dev_cert_crt_start");
extern const uint8_t dev_cert_crt_end[] asm("_binary_dev_cert_crt_end");
extern const uint8_t dev_private_key_start[] asm("_binary_dev_private_key_start");
extern const uint8_t dev_private_key_end[] asm("_binary_dev_private_key_end");
#endif

static const char *TAG = "esp_qcloud_device";

esp_err_t esp_qcloud_device_add_fw_version(const char *version)
{
    ESP_QCLOUD_PARAM_CHECK(version);

    g_device_profile->version = strdup(version);

    return ESP_OK;
}

esp_err_t esp_qcloud_device_secret(const char *device_secret)
{
    ESP_QCLOUD_PARAM_CHECK(device_secret && strlen(device_secret) == DEVICE_SECRET_SIZE);

    return ESP_OK;
}

esp_err_t esp_qcloud_device_cert(const char *cert_crt, const char *private_key)
{
    ESP_QCLOUD_PARAM_CHECK(cert_crt);
    ESP_QCLOUD_PARAM_CHECK(private_key);

    g_device_profile->cert_crt    = strdup(cert_crt);
    g_device_profile->private_key = strdup(private_key);
    g_device_profile->auth_mode   = QCLOUD_AUTH_MODE_CERT;

    return ESP_OK;
}

esp_err_t esp_qcloud_create_device()
{
    g_device_profile = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_profile_t));

#ifdef CONFIG_QCLOUD_MASS_MANUFACTURE
    g_device_profile->auth_mode = QCLOUD_AUTH_MODE_KEY;
    /**
     * @brief Read device configuration information through flash
     *        1. Configure device information via single_mfg_config.csv
     *        2. Generate single_mfg_config.bin, use the following command:
     *          python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate single_mfg_config.csv single_mfg_config.bin 0x4000
     *        3. Burn single_mfg_config.bin to flash
     *          python $IDF_PATH/components/esptool_py/esptool/esptool.py write_flash 0x15000 single_mfg_config.bin
     * @note Currently does not support the use of certificates for mass manufacture
     */
    esp_err_t err = ESP_FAIL;
    nvs_handle handle = 0;
    size_t required_size = 0;
    ESP_ERROR_CHECK(nvs_flash_init_partition(CONFIG_QCLOUD_FACTRY_PARTITION_NAME));
    err = nvs_open_from_partition(CONFIG_QCLOUD_FACTRY_PARTITION_NAME,
                                  CONFIG_QCLOUD_FACTRY_PARTITION_NAMESPACE, NVS_READONLY, &handle);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, ERR_EXIT, "<%s> Factory partition information is not burned", esp_err_to_name(err));

    required_size = PRODUCT_ID_SIZE + 1;
    g_device_profile->product_id = ESP_QCLOUD_CALLOC(1, PRODUCT_ID_SIZE + 1);
    ESP_ERROR_CHECK(nvs_get_str(handle, "product_id", g_device_profile->product_id, &required_size));

    required_size = DEVICE_NAME_MAX_SIZE + 1;
    g_device_profile->device_name = ESP_QCLOUD_CALLOC(1, DEVICE_NAME_MAX_SIZE + 1);
    ESP_ERROR_CHECK(nvs_get_str(handle, "device_name", g_device_profile->device_name, &required_size));

    required_size = DEVICE_SECRET_SIZE + 1;
    g_device_profile->device_secret = ESP_QCLOUD_CALLOC(1, DEVICE_SECRET_SIZE + 1);
    ESP_ERROR_CHECK(nvs_get_str(handle, "device_secret", g_device_profile->device_secret, &required_size));
#else

    g_device_profile->product_id    = CONFIG_QCLOUD_PRODUCT_ID;
    g_device_profile->device_name   = CONFIG_QCLOUD_DEVICE_NAME;

#ifdef CONFIG_AUTH_MODE_KEY
    g_device_profile->auth_mode = QCLOUD_AUTH_MODE_KEY;
    /**
     * @brief Read device configuration information through sdkconfig.h
     *        1. Configure device information via `idf.py menuconfig`, Menu path: (Top) -> Example Configuration
     *        2. Select key authentication
     *        3. Enter device secret key
     */
    g_device_profile->device_secret = CONFIG_QCLOUD_DEVICE_SECRET;

    if (strlen(g_device_profile->device_secret) != DEVICE_SECRET_SIZE
            || strlen(g_device_profile->product_id) != PRODUCT_ID_SIZE) {
        ESP_LOGE(TAG, "Please check if the authentication information of the device is configured");
        ESP_LOGE(TAG, "Obtain authentication configuration information from login qcloud iothut: ");
        ESP_LOGE(TAG, "https://console.cloud.tencent.com/iotexplorer");
        ESP_LOGE(TAG, "product_id: %s", g_device_profile->product_id);
        ESP_LOGE(TAG, "device_name: %s", g_device_profile->device_name);
        ESP_LOGE(TAG, "device_secret: %s", g_device_profile->device_secret);
        goto ERR_EXIT;
    }
#endif

#ifdef CONFIG_AUTH_MODE_CERT
    g_device_profile->auth_mode = QCLOUD_AUTH_MODE_CERT;
    /**
     * @brief Read device configuration information through sdkconfig.h
     *        1. Configure device information via `idf.py menuconfig`, Menu path: (Top) -> Example Configuration
     *        2. Choose certificate authentication
     *        3. Replace the certificate file in the config directory
     */

    g_device_profile->cert_crt = (char*)dev_cert_crt_start;
    g_device_profile->private_key = (char*)dev_private_key_start;

    if (strlen(g_device_profile->product_id) != PRODUCT_ID_SIZE
        || strlen(g_device_profile->cert_crt) == DEVICE_CERT_FILE_DEFAULT_SIZE) {
        ESP_LOGE(TAG, "Please check if the authentication information of the device is configured");
        ESP_LOGE(TAG, "Obtain authentication configuration information from login qcloud iothut: ");
        ESP_LOGE(TAG, "https://console.cloud.tencent.com/iotexplorer");
        ESP_LOGE(TAG, "product_id: %s", g_device_profile->product_id);
        ESP_LOGE(TAG, "device_name: %s", g_device_profile->device_name);
        ESP_LOGE(TAG, "cert_crt: \r\n%s", g_device_profile->cert_crt);
        ESP_LOGE(TAG, "private_key: \r\n%s", g_device_profile->private_key);
        goto ERR_EXIT;
    }
#endif

#endif

    return ESP_OK;

ERR_EXIT:
    ESP_QCLOUD_FREE(g_device_profile);
    vTaskDelay(pdMS_TO_TICKS(3000));
    return ESP_FAIL;
}

const char *esp_qcloud_get_device_name()
{
    return g_device_profile->device_name;
}

const char *esp_qcloud_get_version()
{
    return g_device_profile->version;
}

const char *esp_qcloud_get_product_id()
{
    return g_device_profile->product_id;
}

esp_qcloud_auth_mode_t esp_qcloud_get_auth_mode()
{
    return g_device_profile->auth_mode;
}

const char *esp_qcloud_get_device_secret()
{
    return g_device_profile->device_secret;
}

const char *esp_qcloud_get_cert_crt()
{
    return g_device_profile->cert_crt;
}

const char *esp_qcloud_get_private_key()
{
    return g_device_profile->private_key;
}

esp_err_t esp_qcloud_device_add_property(const char *id, esp_qcloud_param_val_type_t type)
{
    esp_qcloud_param_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));

    item->id = strdup(id);
    item->value.type = type;

    esp_qcloud_param_t *last = SLIST_FIRST(&g_property_list);

    if (last == NULL) {
        SLIST_INSERT_HEAD(&g_property_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_device_add_action_cb(const char *action_id, const esp_qcloud_action_cb_t action_cb)
{
    esp_qcloud_action_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_action_t));

    item->id = strdup(action_id);
    item->action_cb = action_cb;

    esp_qcloud_action_t *last = SLIST_FIRST(&g_action_list);

    if (last == NULL) {
        SLIST_INSERT_HEAD(&g_action_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_device_add_property_cb(const esp_qcloud_get_param_t get_param_cb,
                                   const esp_qcloud_set_param_t set_param_cb)
{
    g_esp_qcloud_get_param = get_param_cb;
    g_esp_qcloud_set_param = set_param_cb;

    return ESP_OK;
}

esp_err_t esp_qcloud_handle_set_param(const cJSON *request_params, cJSON *reply_data)
{
    esp_err_t err = ESP_FAIL;

    for (cJSON *item = request_params->child; item; item = item->next) {
        esp_qcloud_param_val_t value = {0};

        switch (item->type) {
            case cJSON_False:
                value.b = false;
                break;

            case cJSON_True:
                value.b = true;
                break;

            case cJSON_Number:
                value.i = item->valueint;
                value.f = item->valuedouble;
                break;

            case cJSON_String:
                value.s = item->valuestring;
                break;

            default:
                break;
        }

        err = g_esp_qcloud_set_param(item->string, &value);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_OK, "<%s> esp_qcloud_set_param, id: %s",
                               esp_err_to_name(err), item->string);
    }

    return err;
}

esp_err_t esp_qcloud_handle_get_param(const cJSON *request_data, cJSON *reply_data)
{
    esp_err_t err = ESP_FAIL;

    esp_qcloud_param_t *param;
    SLIST_FOREACH(param, &g_property_list, next) {
        /* Check if command starts with buf */
        err = g_esp_qcloud_get_param(param->id, &param->value);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_OK, "esp_qcloud_get_param, id: %s", param->id);

        if (param->value.type == QCLOUD_VAL_TYPE_INTEGER) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.i);
        } else if (param->value.type == QCLOUD_VAL_TYPE_BOOLEAN) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.b);
        } else if (param->value.type == QCLOUD_VAL_TYPE_STRING) {
            cJSON_AddStringToObject(reply_data, param->id, param->value.s);
        } else if (param->value.type == QCLOUD_VAL_TYPE_FLOAT) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.f);
        }
    }

    return err;
}

esp_err_t esp_qcloud_operate_action(esp_qcloud_method_t *action_handle, const char *action_id, char *params)
{
    esp_err_t err = ESP_ERR_NOT_FOUND;

    esp_qcloud_action_t *action;
    SLIST_FOREACH(action, &g_action_list, next) {
        if(!strcmp(action->id, action_id)){
            return action->action_cb(action_handle, params);
        }
    }
    ESP_LOGE(TAG, "The callback function of <%s> was not found, Please check <esp_qcloud_device_add_action_cb>", action_id);
    return err;
}
