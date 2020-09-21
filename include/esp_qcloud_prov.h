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

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>

#include "esp_smartconfig.h"

typedef enum {
    SOFTAPCONFIG_TYPE_ESPRESSIF = 1,       /**< protocol: LeXin */
    SOFTAPCONFIG_TYPE_TENCENT,         /**< protocol: Tencent */
    SOFTAPCONFIG_TYPE_ESPRESSIF_TENCENT,   /**< protocol: LeXin and Tencent */
} softapconfig_type_t;

bool esp_qcloud_prov_is_provisioned(void);
esp_err_t esp_qcloud_wifi_init(void);
esp_err_t esp_qcloud_wifi_start(const wifi_config_t *sta_cfg);

esp_err_t esp_qcloud_prov_smartconfig_start(smartconfig_type_t type);
esp_err_t esp_qcloud_prov_smartconfig_stop(void);

esp_err_t esp_qcloud_prov_softapconfig_start(softapconfig_type_t type, const char *ssid, const char *password);
esp_err_t esp_qcloud_prov_softapconfig_stop(void);

esp_err_t esp_qcloud_prov_wait(wifi_config_t *sta_cfg, uint32_t wait_ms);

void esp_qcloud_prov_print_wechat_qr(const char *name, const char *transport);

#ifdef __cplusplus
}
#endif
