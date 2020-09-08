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
#include <sdkconfig.h>
#include <string.h>
#include <esp_log.h>

#include <esp_qcloud_iothub.h>
#include "esp_qcloud_utils.h"

// #include "esp_qcloud_internal.h"

/* Handle to maintain internal information (will move to an internal file) */
typedef struct {
    const char *product_id;
    const char *version;
    esp_qcloud_auth_mode_t auth_mode;
    const char *device_name;
    union {
        const char *device_secret;
        struct  {
            const char *cert_crt;
            const char *private_key;
        };
    };
} esp_qcloud_profile_t;

static esp_qcloud_profile_t *g_device_profile = NULL;
static esp_qcloud_set_param_t esp_qcloud_set_param = NULL;
static esp_qcloud_get_param_t esp_qcloud_get_param = NULL;

static const char *TAG = "esp_qcloud_device";

typedef struct esp_qcloud_param {
    const char *id;
    esp_qcloud_param_val_t value;
    SLIST_ENTRY(esp_qcloud_param) next;    //!< next command in the list
} esp_qcloud_param_t;

static SLIST_HEAD(param_list_, esp_qcloud_param) g_param_list;

esp_err_t esp_qcloud_create_device(const char *product_id, const char *device_name)
{
    ESP_QCLOUD_PARAM_CHECK(product_id);
    ESP_QCLOUD_PARAM_CHECK(device_name);

    g_device_profile = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));

    g_device_profile->device_name = device_name;
    g_device_profile->product_id  = product_id;

    return ESP_OK;
}

esp_err_t esp_qcloud_device_version(const char *version)
{
    ESP_QCLOUD_PARAM_CHECK(version);

    g_device_profile->version = version;

    return ESP_OK;
}

esp_err_t esp_qcloud_device_secret(const char *device_secret)
{
    ESP_QCLOUD_PARAM_CHECK(device_secret);

    g_device_profile->device_secret = device_secret;
    g_device_profile->auth_mode     = QCLOUD_AUTH_MODE_KEY;

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

esp_qcloud_param_val_t esp_qcloud_bool(bool val)
{
    esp_qcloud_param_val_t param_val = {
        .type = QCLOUD_VAL_TYPE_BOOLEAN,
        .b = val
    };
    return param_val;
}

esp_qcloud_param_val_t esp_qcloud_int(int val)
{
    esp_qcloud_param_val_t param_val = {
        .type = QCLOUD_VAL_TYPE_INTEGER,
        .i = val
    };
    return param_val;
}

esp_qcloud_param_val_t esp_qcloud_float(float val)
{
    esp_qcloud_param_val_t param_val = {
        .type = QCLOUD_VAL_TYPE_FLOAT,
        .f = val
    };
    return param_val;
}

esp_qcloud_param_val_t esp_qcloud_str(const char *val)
{
    esp_qcloud_param_val_t param_val = {
        .type = QCLOUD_VAL_TYPE_STRING,
        .s = (char *)val
    };
    return param_val;
}

esp_err_t esp_qcloud_device_param(const char *id, esp_qcloud_param_val_t default_value)
{
    esp_qcloud_param_t *item = ESP_QCLOUD_MALLOC(sizeof(esp_qcloud_param_t));
    item->id = strdup(id);
    item->value = default_value;

    esp_qcloud_param_t *last = SLIST_FIRST(&g_param_list);

    if (last == NULL) {
        SLIST_INSERT_HEAD(&g_param_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_device_handle(const esp_qcloud_get_param_t get_param_cb,
                                   const esp_qcloud_set_param_t set_param_cb)
{
    esp_qcloud_get_param = get_param_cb;
    esp_qcloud_set_param = set_param_cb;

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

        err = esp_qcloud_set_param(item->string, &value);
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
        err = esp_qcloud_get_param(param->id, &param->value);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_OK, "esp_qcloud_get_param, id: %s", param->id);

        if (param->value.type == QCLOUD_VAL_TYPE_INTEGER || param->value.type == QCLOUD_VAL_TYPE_BOOLEAN) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.i);
        } else if (param->value.type == QCLOUD_VAL_TYPE_STRING) {
            cJSON_AddStringToObject(reply_data, param->id, param->value.s);
            ESP_QCLOUD_FREE(param->value.s);
        } else if (param->value.type == QCLOUD_VAL_TYPE_FLOAT) {
            cJSON_AddNumberToObject(reply_data, param->id, param->value.f);
        }
    }

    return err;
}
