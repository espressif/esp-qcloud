// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
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

/**
 * @brief   Initialize console module
 *          - Initialize the console
 *          - Register help commands
 *          - Initialize filesystem
 *          - Create console handle task
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t esp_qcloud_console_init(void);

/**
 * @brief   De-initialize console module
 *          Call this once when done using console module functions
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t esp_qcloud_console_deinit(void);

/**
 * @brief   Register frequently used system commands
 *          - version: Get version of chip and SDK
 *          - heap: Get the current size of free heap memory
 *          - restart: Software reset of the chip
 *          - reset: Clear device configuration information
 *          - log: Set log level for given tag
 *          - coredump: Get core dump information
 */
void esp_qcloud_commands_register_common(void);

#ifdef __cplusplus
}
#endif /**< _cplusplus */
