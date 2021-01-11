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

#include "esp_qcloud_utils.h"
#include "esp_qcloud_iothub.h"
#include "esp_qcloud_mqtt.h"

#define OTA_REBOOT_TIMER_SEC  10

static const char *TAG = "esp_qcloud_ota";

typedef enum {
    QCLOUD_OTA_REPORT_FAIL            = -1,
    QCLOUD_OTA_REPORT_NONE            = 0,
    QCLOUD_OTA_REPORT_VERSION         = 1,
    QCLOUD_OTA_REPORT_DOWNLOAD_BEGIN  = 2,
    QCLOUD_OTA_REPORT_DOWNLOADING     = 3,
    QCLOUD_OTA_REPORT_BURN_BEGIN      = 4,
    QCLOUD_OTA_REPORT_BURN_SUCCESS    = 5,
} esp_qcloud_ota_report_type_t;

typedef struct {
    size_t file_size;
    char md5sum[33];
    char url[512];
    char version[32];
    uint32_t start_timetemp;
    size_t download_size;
    uint8_t download_percent;
} esp_qcloud_ota_info_t;

static esp_err_t esp_qcloud_ota_report_status(esp_qcloud_ota_info_t *ota_info, esp_qcloud_ota_report_type_t type, const char *result_msg)
{
    esp_err_t err       = ESP_FAIL;
    char *publish_topic = NULL;
    char *publish_data  = NULL;
    const char *result_code = "0";
    result_msg = result_msg ? result_msg : "";

    cJSON *json_publish_data = cJSON_CreateObject();
    cJSON *progress = cJSON_CreateObject();
    cJSON *report = cJSON_CreateObject();

    switch (type) {
        case QCLOUD_OTA_REPORT_DOWNLOADING: {
            char str_tmp[16] = {0};
            cJSON_AddStringToObject(progress, "state", "downloading");
            cJSON_AddStringToObject(progress, "percent", itoa(ota_info->download_percent, str_tmp, 10));
            break;
        }

        case QCLOUD_OTA_REPORT_BURN_BEGIN:
            cJSON_AddStringToObject(progress, "state", "burning");
            break;

        case QCLOUD_OTA_REPORT_BURN_SUCCESS:
            cJSON_AddStringToObject(progress, "state", "done");
            break;

        case QCLOUD_OTA_REPORT_FAIL:
            cJSON_AddStringToObject(progress, "state", "fail");
            result_code = "-1";
            break;

        default:
            break;
    }

    cJSON_AddStringToObject(progress, "result_code", result_code);
    cJSON_AddStringToObject(progress, "result_msg", result_msg);
    cJSON_AddItemToObject(report, "progress", progress);
    cJSON_AddStringToObject(report, "version", ota_info->version);
    cJSON_AddStringToObject(json_publish_data, "type", "report_progress");
    cJSON_AddItemToObject(json_publish_data, "report", report);
    publish_data = cJSON_PrintUnformatted(json_publish_data);

    cJSON_Delete(json_publish_data);

    asprintf(&publish_topic, "$ota/report/%s/%s",
             esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

    err = esp_qcloud_mqtt_publish(publish_topic, publish_data, strlen(publish_data));
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> Publish to %s, data: %s",
                          esp_err_to_name(err), publish_topic,  publish_data);

    ESP_LOGI(TAG, "mqtt_publish, topic: %s, data: %s", publish_topic, publish_data);

EXIT:
    ESP_QCLOUD_FREE(publish_topic);
    ESP_QCLOUD_FREE(publish_data);

    return err;
}

static esp_err_t validate_image_header(esp_qcloud_ota_info_t *ota_info, esp_app_desc_t *new_app_info)
{
    ESP_QCLOUD_PARAM_CHECK(new_app_info);

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;

    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGD(TAG, "Running firmware version: %s", running_app_info.version);
    }

#ifndef CONFIG_QCLOUD_SKIP_VERSION_CHECK

    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) == 0) {
        ESP_LOGW(TAG, "Current running version is same as the new. We will not continue the update.");

        if (ota_info) {
            esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_FAIL, "Same version received");
        }

        return ESP_FAIL;
    }

#endif

#ifndef CONFIG_QCLOUD_SKIP_PROJECT_NAME_CHECK

    if (memcmp(new_app_info->project_name, running_app_info.project_name, sizeof(new_app_info->project_name)) != 0) {
        ESP_LOGW(TAG, "OTA Image built for Project: %s. Expected: %s",
                 new_app_info->project_name, running_app_info.project_name);

        if (ota_info) {
            esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_FAIL, "Project Name mismatch");
        }

        return ESP_FAIL;
    }

#endif

    return ESP_OK;
}

static char *ota_url_https_to_http(const char *url)
{
    char *p = (char *)url;
    char *result;

    if(!strstr(p, "https") || strlen(p) <= 5){
        return NULL;
    }
    asprintf(&result, "http%s", p+5);

    return result;
}

static void esp_qcloud_iotbub_ota_task(void *arg)
{
    esp_err_t ota_finish_err = 0;
    esp_qcloud_ota_info_t *ota_info = (esp_qcloud_ota_info_t *)arg;
    esp_http_client_config_t http_config = {
        .url = ota_info->url,
        .timeout_ms = 5000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
        .skip_cert_common_name_check = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    /*< Using a warning just to highlight the message */
    ESP_LOGW(TAG, "Starting OTA. This may take time.");

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
        esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_FAIL, "ESP HTTPS OTA Begin failed");
        goto EXIT;
    }

    esp_app_desc_t app_desc = {0};
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
        esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_FAIL, "Failed to read image decription");
        goto EXIT;
    }

    err = validate_image_header(ota_info, &app_desc);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "image header verification failed");

    esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_DOWNLOADING, "Downloading Firmware Image");

    for (int report_percent = 10;;) {
        static int last_size = 0;
        err = esp_https_ota_perform(https_ota_handle);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_ERR_HTTPS_OTA_IN_PROGRESS, "esp_https_ota_perform");

        ota_info->download_size = esp_https_ota_get_image_len_read(https_ota_handle);
        ESP_QCLOUD_ERROR_GOTO(ota_info->download_size == last_size, EXIT, "esp_https_ota_perform");
        ota_info->download_percent = ota_info->download_size * 100 / ota_info->file_size;

        last_size = ota_info->download_size;

        if (report_percent == ota_info->download_percent) {
            esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_DOWNLOADING, "Firmware Image downloading.......");
            ESP_LOGI(TAG, "Downloading Firmware Image, size: %d, percent: %d%%", ota_info->download_size, ota_info->download_percent);

            report_percent += 10;
        }
    }

    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
        // the OTA image was not completely received and user can customise the response to this situation.
        ESP_LOGE(TAG, "Complete data was not received.");
    }

    esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_BURN_BEGIN, "Firmware Image download complete");


EXIT:
    ota_finish_err = esp_https_ota_finish(https_ota_handle);

    if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
        ESP_LOGI(TAG, "OTA upgrade successful. Rebooting in %d seconds...", OTA_REBOOT_TIMER_SEC);
        esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_BURN_SUCCESS, "OTA Upgrade finished successfully");
        esp_qcloud_reboot(OTA_REBOOT_TIMER_SEC);
    } else {
        if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
            esp_qcloud_ota_report_status(ota_info, QCLOUD_OTA_REPORT_FAIL, "Image validation failed");
        } else {
            /* Not reporting status here, because relevant error will already be reported
             * in some earlier step
             */
            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed %d", ota_finish_err);
        }
    }

    ESP_QCLOUD_FREE(ota_info);
    vTaskDelete(NULL);
}

static void esp_qcloud_iothub_ota_callback(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    ESP_LOGI(TAG, "ota_callback, topic: %s, payload: %.*s", topic, payload_len, (char *)payload);

    cJSON *request_data = cJSON_Parse(payload);
    ESP_QCLOUD_ERROR_GOTO(!request_data, EXIT, "The data format is wrong and cannot be parsed");

    if (!strcasecmp(cJSON_GetObjectItem(request_data, "type")->valuestring, "update_firmware")) {
        esp_qcloud_ota_info_t *ota_info = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_ota_info_t));
        ota_info->file_size = cJSON_GetObjectItem(request_data, "file_size")->valueint;
        strcpy(ota_info->md5sum, cJSON_GetObjectItem(request_data, "md5sum")->valuestring);

#ifdef CONFIG_QCLOUD_USE_HTTPS_UPDATA
        strcpy(ota_info->url, cJSON_GetObjectItem(request_data, "url")->valuestring);     
#else
        char *http_url = ota_url_https_to_http(cJSON_GetObjectItem(request_data, "url")->valuestring);
        strcpy(ota_info->url, http_url);
        ESP_QCLOUD_FREE(http_url);
#endif

        strcpy(ota_info->version, cJSON_GetObjectItem(request_data, "version")->valuestring);

        xTaskCreatePinnedToCore(esp_qcloud_iotbub_ota_task, "iotbub_ota", 4 * 1024, ota_info, 1, NULL, 0);
    }

    ESP_LOGW(TAG, "esp_qcloud_iothub_ota_callback exit");

EXIT:
    cJSON_Delete(request_data);
}

esp_err_t esp_qcloud_iothub_ota_enable()
{
    esp_err_t err         = ESP_OK;
    char *publish_topic   = NULL;
    char *publish_data    = NULL;
    char *subscribe_topic = NULL;

    /**
     * @brief ubscribed server firmware upgrade news
     */
    asprintf(&subscribe_topic, "$ota/update/%s/%s",
             esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    err = esp_qcloud_mqtt_subscribe(subscribe_topic, esp_qcloud_iothub_ota_callback, NULL);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> subscribe to %s", esp_err_to_name(err), subscribe_topic);

    ESP_LOGI(TAG, "mqtt_subscribe, topic: %s", subscribe_topic);

    /**
     * @breif The device reports the current version number
     */
    asprintf(&publish_topic, "$ota/report/%s/%s",
             esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    asprintf(&publish_data, "{\"type\":\"report_version\",\"report\":{\"version\":\"%s\"}}", esp_qcloud_get_version());
    err = esp_qcloud_mqtt_publish(publish_topic, publish_data, strlen(publish_data));
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> Publish to %s, data: %s",
                          esp_err_to_name(err), publish_topic,  publish_data);
    ESP_LOGI(TAG, "mqtt_publish, topic: %s, data: %s", publish_topic, publish_data);

EXIT:
    ESP_QCLOUD_FREE(publish_topic);
    ESP_QCLOUD_FREE(publish_data);
    ESP_QCLOUD_FREE(subscribe_topic);
    return err;
}
