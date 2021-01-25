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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "esp_log.h"

#include "esp_qcloud_utils.h"
#include "esp_qcloud_mem.h"

/**
 *@brief Configuration mdebug print enable, whether the output information according to the client needs to know.
 *       please assign CONFIG_QCLOUD_LOG_PRINTF_ENABLE a value.
 */
#ifdef CONFIG_QCLOUD_LOG_PRINTF_ENABLE
#define ESP_QCLOUD_LOG_PRINTF(fmt, ...) printf("D [%s, %d]: " fmt, TAG, __LINE__, __VA_ARGS__)
#else
#define ESP_QCLOUD_LOG_PRINTF(fmt, ...)
#endif

#define ESP_QCLOUD_LOG_CALLOC calloc
#define ESP_QCLOUD_LOG_MALLOC malloc
#define ESP_QCLOUD_LOG_FREE   free

/**
 * @brief Log sending configuration
 */
typedef struct {
    esp_log_level_t log_level_uart;
    esp_log_level_t log_level_flash;
    esp_log_level_t log_level_iothub;
    esp_log_level_t log_level_local;
} esp_qcloud_log_config_t;

/**
 * @brief Set the send type
 *
 */
typedef enum {
    MDEBUG_LOG_TYPE_ESPNOW = 1 << 1,
    MDEBUG_LOG_TYPE_FLASH  = 1 << 2,
} esp_qcloud_log_type_t;

/**
 * @brief Type of log storage queue
 */
typedef struct {
    uint16_t size;              /**< The length of the log data */
    esp_qcloud_log_type_t type; /**< Ways to send logs */
    char data[0];               /**< Log data */
} esp_qcloud_log_queue_t;

/**
 * @brief  Get the configuration of the log during wireless debugging
 *
 * @param  config The configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t esp_qcloud_log_get_config(esp_qcloud_log_config_t *config);

/**
 * @brief  Set the configuration of the log during wireless debugging
 *
 * @param  config The configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t esp_qcloud_log_set_config(const esp_qcloud_log_config_t *config);

/**
 * @brief Init log mdebug
 *        - Set log mdebug configuration
 *        - Create log mdebug task
 *
 * @return
 *     - ESP_OK
 */
esp_err_t esp_qcloud_log_init(const esp_qcloud_log_config_t *config);

/**
 * @brief De-initialize log mdebug
 *      Call this once when done using log mdebug functions
 *
 * @return
 *     - ESP_OK
 */
esp_err_t esp_qcloud_log_deinit(void);

/**
 * @brief  Send log information to Tencent Cloud server
 *
 * @param  config The configuration of the log
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t esp_qcloud_log_iothub_write(const char *data, size_t size, esp_log_level_t level, const struct tm *log_time);

/**
 * @brief Read memory data in flash
 *
 * @param data  Data from the flash's spiffs files in the log
 * @param size  Size from the flash's spiffs files in the log
 *
 * @return
 *      - ESP_OK
 *      - read_size
 */
esp_err_t esp_qcloud_log_flash_read(char *data, size_t *size);

/**
 * @brief Create files size,For the data to be stored in the file
 *      for subsequent calls.paramters MDEBUG_FLASH_FILE_MAX_NUM
 *      if files sizes change.
 *
 * @return
 *      - size
 */
size_t esp_qcloud_log_flash_size(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
