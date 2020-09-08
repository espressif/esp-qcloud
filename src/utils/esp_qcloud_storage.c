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
#include "string.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>

static const char *TAG = "esp_qcloud_storage";

#define CREDENTIALS_NAMESPACE  "qcloud_device"

esp_err_t esp_qcloud_storage_init()
{
    static bool s_storage_init_flag = false;

    if (s_storage_init_flag) {
        ESP_LOGW(TAG, "ESP QCloud Storage already initialised");
        return ESP_OK;
    }

    /* Initialize NVS. */
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    err = nvs_flash_init_partition(CONFIG_ESP_QCLOUD_DEVICE_INFO_PARTITION_NAME);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS Flash init failed, err_str: %s", esp_err_to_name(err));
    } else {
        s_storage_init_flag = true;
    }

    return err;
}

void *esp_qcloud_storage_get(const char *key)
{
    esp_err_t err = ESP_FAIL;
    nvs_handle handle = 0;
    void *value = NULL;
    size_t required_size = 0;

    if ((err = nvs_open_from_partition(CONFIG_ESP_QCLOUD_DEVICE_INFO_PARTITION_NAME, CREDENTIALS_NAMESPACE,
                                       NVS_READONLY, &handle)) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open for %s %s %s failed with error %d, str_err: %s",
                 CONFIG_ESP_QCLOUD_DEVICE_INFO_PARTITION_NAME, CREDENTIALS_NAMESPACE, key, err, esp_err_to_name(err));
        return NULL;
    }

    if ((err = nvs_get_blob(handle, key, NULL, &required_size)) == ESP_OK) {
        value = calloc(required_size + 1, 1); /* + 1 for NULL termination */
        nvs_get_blob(handle, key, value, &required_size);
    } else if ((err = nvs_get_str(handle, key, NULL, &required_size)) == ESP_OK) {
        value = calloc(required_size + 1, 1); /* + 1 for NULL termination */
        nvs_get_str(handle, key, value, &required_size);
    }

    nvs_close(handle);
    return value;
}

esp_err_t esp_qcloud_storage_set(const char *key, void *data, size_t len)
{
    nvs_handle handle;
    esp_err_t err;

    if ((err = nvs_open_from_partition(CONFIG_ESP_QCLOUD_DEVICE_INFO_PARTITION_NAME, CREDENTIALS_NAMESPACE,
                                       NVS_READWRITE, &handle)) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return ESP_FAIL;
    }

    if ((err = nvs_set_blob(handle, key, data, len)) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write key %s with error %d size %d", key, err, len);
        nvs_close(handle);
        return ESP_FAIL;
    }

    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t esp_qcloud_storage_erase(const char *key)
{
    nvs_handle handle;
    esp_err_t err;

    if ((err = nvs_open_from_partition(CONFIG_ESP_QCLOUD_DEVICE_INFO_PARTITION_NAME, CREDENTIALS_NAMESPACE,
                                       NVS_READWRITE, &handle)) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed with error %d", err);
        return ESP_FAIL;
    }

    /**
     * @brief If key is CREDENTIALS_NAMESPACE, erase all info in CREDENTIALS_NAMESPACE
     */
    if (!strcmp(key, CREDENTIALS_NAMESPACE)) {
        err = nvs_erase_all(handle);
    } else {
        err = nvs_erase_key(handle, key);
    }

    nvs_commit(handle);
    nvs_close(handle);
    return err;
}
