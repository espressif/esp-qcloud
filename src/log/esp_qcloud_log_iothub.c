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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_https_ota.h>
#include <esp_wifi_types.h>
#include <esp_wifi.h>

#include "cJSON.h"

#include "esp_qcloud_iothub.h"
#include "esp_qcloud_mqtt.h"
#include "esp_qcloud_log.h"

#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/error.h"

#define LOG_UPLOAD_SERVER_URL "http://devicelog.iot.cloud.tencent.com/cgi-bin/report-log"
#define MAX_HTTP_OUTPUT_BUFFER 128

static const char *TAG = "esp_qcloud_log_iothub";
typedef struct {
    char signature[40];
    char ctrl_bytes[4];
    char product_id[10];
    char device_name[48];
    char timestamp[10];
    char log_level[3];
    char log_time[21];
    char data[];
} esp_qcloud_log_iothub_t;

esp_err_t esp_qcloud_log_iothub_write(const char *data, size_t size, esp_log_level_t log_level, const struct tm *log_time)
{
    ESP_QCLOUD_PARAM_CHECK(data);
    ESP_QCLOUD_PARAM_CHECK(size);

    if (!esp_qcloud_iothub_is_connected()) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = ESP_OK;
    char timestamp[22] = {0};
    char *level_str[] = {"DIS", "ERR", "WRN", "INF", "DBG"};
    mbedtls_md_context_t sha_ctx;
    char *response_data = NULL;
    size_t log_size = sizeof(esp_qcloud_log_iothub_t) + size;
    esp_qcloud_log_iothub_t *log_data = ESP_QCLOUD_LOG_MALLOC(log_size + 1);

    /**
     * @breif Construct iothub log data
     */
    memset(log_data, '#', sizeof(esp_qcloud_log_iothub_t));
    log_data->ctrl_bytes[0] = 'P';
    memcpy(log_data->product_id, esp_qcloud_get_product_id(), strlen(esp_qcloud_get_product_id()));
    memcpy(log_data->device_name, esp_qcloud_get_device_name(), strlen(esp_qcloud_get_device_name()));
    sprintf(timestamp, "%010lu", mktime((struct tm *)log_time));
    memcpy(log_data->timestamp, timestamp, sizeof(log_data->timestamp));
    sprintf(log_data->log_level, "%s", level_str[log_level]);
    strftime(timestamp, sizeof(timestamp), "|%F %T|", log_time);
    memcpy(log_data->log_time, timestamp, sizeof(log_data->log_time));

    memcpy(log_data->data, data, size);
    log_data->data[size] = 0;

    uint8_t digest[20] = {0};
    char digest_str[41] = {0};
    mbedtls_md_init(&sha_ctx);
    err = mbedtls_md_setup(&sha_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 1);
    ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_setup");
    err = mbedtls_md_hmac_starts(&sha_ctx, (const uint8_t *)esp_qcloud_get_device_secret(), strlen(esp_qcloud_get_device_secret()));
    ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_hmac_starts");
    err = mbedtls_md_hmac_update(&sha_ctx, (uint8_t *)log_data->ctrl_bytes, log_size - sizeof(log_data->signature));
    ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_hmac_update");
    err = mbedtls_md_hmac_finish(&sha_ctx, digest);
    ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_hmac_finish");

    for (int i = 0; i < sizeof(digest); i++) {
        sprintf(digest_str + i * 2, "%02x", digest[i]);
    }

    memcpy(log_data->signature, digest_str, sizeof(log_data->signature));

    /**
     * @breif Send log data to iothub
     */
    static esp_http_client_handle_t s_http_client = NULL;

    if (!s_http_client) {
        esp_http_client_config_t config = {
            .url    = LOG_UPLOAD_SERVER_URL,
            .method =  HTTP_METHOD_POST,
        };

        s_http_client = esp_http_client_init(&config);
    }

    ESP_QCLOUD_LOG_PRINTF("Log HTTP stream writer data: %s\n", (char *)log_data);
    esp_http_client_set_post_field(s_http_client, (char *)log_data, log_size);

    err = esp_http_client_perform(s_http_client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "<%s> HTTP POST request failed", esp_err_to_name(err));
        esp_http_client_close(s_http_client);
        esp_http_client_cleanup(s_http_client);
        s_http_client = NULL;
        goto EXIT;
    }

    response_data = ESP_QCLOUD_LOG_CALLOC(1, MAX_HTTP_OUTPUT_BUFFER);
    int response_len = esp_http_client_read_response(s_http_client, response_data, MAX_HTTP_OUTPUT_BUFFER);
    ESP_QCLOUD_ERROR_GOTO(response_len < 0, EXIT, "Error read data, err_str: %s", strerror(errno));

    ESP_QCLOUD_LOG_PRINTF("Log HTTP stream reader data: %s\n", response_data);

EXIT:
    mbedtls_md_free(&sha_ctx);
    ESP_QCLOUD_LOG_FREE(log_data);
    ESP_QCLOUD_LOG_FREE(response_data);
    return err;
}
