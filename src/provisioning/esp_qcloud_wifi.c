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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_qcloud_storage.h"
#include "esp_qcloud_utils.h"

#define QCLOUD_PROV_EVENT_STA_CONNECTED  BIT0

static const char *TAG  = "esp_qcloud_wifi";
static EventGroupHandle_t s_wifi_event_group = NULL;

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(s_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "Disconnect reason : %d", disconnected->reason);
        if(disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT){
            ESP_LOGE(TAG, "wrong password");
            return;
        }
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "STA Connecting to the AP again...");
    }
}

esp_err_t esp_qcloud_wifi_init(void)
{
    if (s_wifi_event_group) {
        return ESP_FAIL;
    }

    s_wifi_event_group = xEventGroupCreate();

    esp_netif_init();

    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    return ESP_OK;
}

esp_err_t esp_qcloud_wifi_start(const wifi_config_t *conf)
{
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, (wifi_config_t *)conf));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Wait for success event */
    xEventGroupWaitBits(s_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED, true, true, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t esp_qcloud_wifi_reset(void)
{
    esp_err_t err = ESP_FAIL;
    err = esp_wifi_restore();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_wifi_restore fail, reason: %s", esp_err_to_name(err));
    
    err = esp_qcloud_storage_erase("wifi_config");
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_storage_erase fail, reason: %s", esp_err_to_name(err));

    return err;
}
