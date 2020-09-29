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

static SLIST_HEAD(param_list_, esp_qcloud_param) g_param_list;

static const char *TAG = "esp_qcloud_device";

typedef struct esp_qcloud_param {
    const char *id;
    esp_qcloud_param_val_t value;
    SLIST_ENTRY(esp_qcloud_param) next;    //!< next command in the list
} esp_qcloud_param_t;

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
    g_device_profile = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));
    g_device_profile->auth_mode = QCLOUD_AUTH_MODE_KEY;

#ifdef CONFIG_QCLOUD_MASS_MANUFACTURE
    /**
     * @brief Read device configuration information through flash
     *        1. Configure device information via single_mfg_config.csv
     *        2. Generate single_mfg_config.bin, use the following command:
     *          python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate single_mfg_config.csv single_mfg_config.bin 0x4000
     *        3. Burn single_mfg_config.bin to flash
     *          python $IDF_PATH/components/esptool_py/esptool/esptool.py write_flash 0x15000 single_mfg_config.bin
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
    /**
     * @brief Read device configuration information through sdkconfig.h
     *        1. Configure device information via `idf.py menuconfig`, Menu path: (Top) -> Example Configuration
     */
    g_device_profile->product_id    = CONFIG_QCLOUD_PRODUCT_ID;
    g_device_profile->device_name   = CONFIG_QCLOUD_DEVICE_NAME;
    g_device_profile->device_secret = CONFIG_QCLOUD_DEVICE_SECRET;
#endif

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

esp_err_t esp_qcloud_device_add_param(const char *id, esp_qcloud_param_val_type_t type)
{
    esp_qcloud_param_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));

    item->id = strdup(id);
    item->value.type = type;

    esp_qcloud_param_t *last = SLIST_FIRST(&g_param_list);

    if (last == NULL) {
        SLIST_INSERT_HEAD(&g_param_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_device_add_cb(const esp_qcloud_get_param_t get_param_cb,
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
    SLIST_FOREACH(param, &g_param_list, next) {
        /* Check if command starts with buf */
        err = g_esp_qcloud_get_param(param->id, &param->value);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_OK, "esp_qcloud_get_param, id: %s", param->id);

        if (param->value.type == QCLOUD_VAL_TYPE_INTEGER) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.i);
        } else if (param->value.type == QCLOUD_VAL_TYPE_BOOLEAN) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.b);
        } else if (param->value.type == QCLOUD_VAL_TYPE_STRING) {
            cJSON_AddStringToObject(reply_data, param->id, param->value.s);
            ESP_QCLOUD_FREE(param->value.s);
        } else if (param->value.type == QCLOUD_VAL_TYPE_FLOAT) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.f);
        }
    }

    return err;
}
