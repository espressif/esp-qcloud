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

#ifndef __ESP_QCLOUD_DEBUG_FLASH_H__
#define __ESP_QCLOUD_DEBUG_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif /**< _cplusplus */

/**
 * @brief Write memory data in flash
 *
 * @note Don't include timestamp in interface input data
 *
 * @param data  Data from the flash's spiffs files in the log
 * @param size  Size from the flash's spiffs files in the log
 *
 * @return
 *      - ESP_OK
 */
esp_err_t esp_qcloud_log_iothub_write(esp_log_level_t log_level, struct tm *log_time, const char *data, size_t size);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
#endif /**< __ESP_QCLOUD_DEBUG_FLASH_H__ */
