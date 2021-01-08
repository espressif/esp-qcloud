// Copyright 2017 Espressif Systems (Shanghai) PTE LTD
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

#include "esp_wifi.h"
#include "esp_console.h"

#include "esp_qcloud_log.h"
#include "esp_qcloud_log_flash.h"
#include "esp_qcloud_storage.h"

#define MDEBUG_LOG_STORE_KEY               "log_config"
#define MDEBUG_LOG_QUEUE_SIZE              (30)
#define MDEBUG_LOG_TIMEOUT_MS              (30 * 1000)
#define MDEBUG_LOG_QUEUE_BUFFER_MAX_SIZE   (10 * 1024)

#define CONFIG_QCLOUD_TASK_DEFAULT_PRIOTY   6
#define CONFIG_QCLOUD_TASK_PINNED_TO_CORE   0
#define CONFIG_QCLOUD_LOG_MAX_SIZE          1024  /**< Set log length size */

static const char *TAG  = "esp_qcloud_log";
static xQueueHandle g_log_queue              = NULL;
static bool g_log_init_flag                  = false;
static esp_qcloud_log_config_t *g_log_config = NULL;

typedef struct log_info {
    struct tm time;
    esp_log_level_t level;
    size_t size;
    char *data;
} log_info_t;

esp_err_t esp_qcloud_log_get_config(esp_qcloud_log_config_t *config)
{
    ESP_QCLOUD_PARAM_CHECK(config);
    ESP_QCLOUD_ERROR_CHECK(!g_log_init_flag, ESP_ERR_NOT_SUPPORTED, "log debugging is not initialized");

    memcpy(config, g_log_config, sizeof(esp_qcloud_log_config_t));
    return ESP_OK;
}

esp_err_t esp_qcloud_log_set_config(const esp_qcloud_log_config_t *config)
{
    ESP_QCLOUD_PARAM_CHECK(config);
    ESP_QCLOUD_ERROR_CHECK(!g_log_init_flag, ESP_ERR_NOT_SUPPORTED, "log debugging is not initialized");

    esp_err_t ret = ESP_OK;

    memcpy(g_log_config, config, sizeof(esp_qcloud_log_config_t));

    return ret;
}

static ssize_t esp_qcloud_log_vprintf(const char *fmt, va_list vp)
{
    size_t log_size = 0;
    log_info_t *log_info = ESP_QCLOUD_LOG_MALLOC(sizeof(log_info_t));
    time_t now = 0;

    time(&now);
    localtime_r(&now, &log_info->time);
    log_info->size  = log_size = vasprintf(&log_info->data, fmt, vp);
    log_info->level = ESP_LOG_NONE;

    /**
     * @brief Remove the header and tail that appear in the string in the log
     *
     */
    if(log_info->size == 0 ) {
        ESP_QCLOUD_LOG_FREE(log_info->data);
        ESP_QCLOUD_LOG_FREE(log_info);
        return 0;
    }
    
    if (log_info->size > 7) {
        uint8_t log_level_index = (log_info->data[0] == '\033') ? 7 : 0;

        switch (log_info->data[log_level_index]) {
            case 'E':
                log_info->level = ESP_LOG_ERROR;
                break;

            case 'W':
                log_info->level = ESP_LOG_WARN;
                break;

            case 'I':
                log_info->level = ESP_LOG_INFO;
                break;

            case 'D':
                log_info->level = ESP_LOG_DEBUG;
                break;

            case 'V':
                log_info->level = ESP_LOG_VERBOSE;
                break;

            default:
                break;
        }
    }

    if (log_info->level <= g_log_config->log_level_uart) {
        vprintf(fmt, vp); /**< Write log data to uart */
    }

    if (!g_log_queue || xQueueSend(g_log_queue, &log_info, 0) == pdFALSE) {
        ESP_QCLOUD_LOG_FREE(log_info->data);
        ESP_QCLOUD_LOG_FREE(log_info);
    }

    return log_size;
}

static void esp_qcloud_log_send_task(void *arg)
{
    log_info_t *log_info = NULL;

    for (; g_log_config;) {
        if (xQueueReceive(g_log_queue, &log_info, pdMS_TO_TICKS(MDEBUG_LOG_TIMEOUT_MS)) != pdPASS) {
            continue;
        }


        if (g_log_config->log_level_flash != ESP_LOG_NONE
                && log_info->level <= g_log_config->log_level_flash) {
            esp_qcloud_log_flash_write(log_info->data, log_info->size, log_info->level, &log_info->time); /**< Write log data to flash */
        }

        if (g_log_config->log_level_iothub != ESP_LOG_NONE
                && log_info->level <= g_log_config->log_level_iothub) {
            esp_qcloud_log_iothub_write(log_info->data, log_info->size, log_info->level, &log_info->time); /**< Write log data to iothub */
        }

        if (g_log_config->log_level_local != ESP_LOG_NONE
                && log_info->level <= g_log_config->log_level_local) {
            // esp_qcloud_debug_local_write(log_data, log_size);  /**< Write log data to local */
        }

        ESP_QCLOUD_LOG_FREE(log_info->data);
        ESP_QCLOUD_LOG_FREE(log_info);
    }

    vTaskDelete(NULL);
}

esp_err_t esp_qcloud_log_init(const esp_qcloud_log_config_t *config)
{
    if (g_log_init_flag) {
        return ESP_FAIL;
    }

    g_log_config = ESP_QCLOUD_LOG_CALLOC(1, sizeof(esp_qcloud_log_config_t));
    ESP_QCLOUD_ERROR_CHECK(!g_log_config, ESP_ERR_NO_MEM, "");
    memcpy(g_log_config, config, sizeof(esp_qcloud_log_config_t));

    esp_qcloud_log_flash_init();

    /**< Register espnow log redirect function */
    esp_log_set_vprintf(esp_qcloud_log_vprintf);

    g_log_queue = xQueueCreate(MDEBUG_LOG_QUEUE_SIZE, sizeof(esp_qcloud_log_queue_t *));
    ESP_QCLOUD_ERROR_CHECK(!g_log_queue, ESP_FAIL, "g_log_queue create fail");

    xTaskCreatePinnedToCore(esp_qcloud_log_send_task, "qcloud_log_send", 3 * 1024,
                            NULL, CONFIG_QCLOUD_TASK_DEFAULT_PRIOTY - 2,
                            NULL, CONFIG_QCLOUD_TASK_PINNED_TO_CORE);


    ESP_LOGI(TAG, "log initialized successfully");

    g_log_init_flag = true;

    return ESP_OK;
}

esp_err_t esp_qcloud_log_deinit()
{
    if (g_log_init_flag) {
        return ESP_FAIL;
    }

    for (log_info_t *log_data = NULL;
            xQueueReceive(g_log_queue, &log_data, 0);) {
        ESP_QCLOUD_LOG_FREE(log_data);
        ESP_QCLOUD_LOG_FREE(log_data->data);
    }

    esp_qcloud_log_flash_deinit();

    g_log_init_flag = false;
    ESP_QCLOUD_LOG_FREE(g_log_config);

    return ESP_OK;
}
