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
#include <freertos/event_groups.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include "esp_smartconfig.h"

#include <wifi_provisioning/manager.h>
#ifdef CONFIG_APP_WIFI_PROV_TRANSPORT_BLE
#include <wifi_provisioning/scheme_ble.h>
#else /* CONFIG_APP_WIFI_PROV_TRANSPORT_SOFTAP */
#include <wifi_provisioning/scheme_softap.h>
#endif /* CONFIG_APP_WIFI_PROV_TRANSPORT_BLE */

#include "esp_qcloud_prov.h"
#include "esp_qcloud_iothub.h"
#include "esp_qcloud_prov_tencent.h"

static const char *TAG = "esp_qcloud_prov_softapconfig";

static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;

        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials"
                     "\n\tSSID: %s\n\tPassword : %s\n\tChannel: %d",
                     (const char *) wifi_sta_cfg->ssid,
                     (const char *) wifi_sta_cfg->password,
                     wifi_sta_cfg->channel);
            break;
        }

        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                     "\n\tPlease reset to factory and retry provisioning",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                     "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
            break;
        }

        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;

        case WIFI_PROV_END:
            /* De-initialize manager once provisioning is finished */
            wifi_prov_mgr_deinit();
            break;

        default:
            break;
    }
}

esp_err_t esp_qcloud_prov_softapconfig_start(softapconfig_type_t type,
        const char *ssid, const char *password)
{
    esp_err_t err   = ESP_OK;
    const char *pop = NULL;

    if ((SOFTAPCONFIG_TYPE_ESPRESSIF & type) || (SOFTAPCONFIG_TYPE_ESPRESSIF_TENCENT & type)) {
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));

        /* Configuration for the provisioning manager */
        wifi_prov_mgr_config_t config = {
            /* What is the Provisioning Scheme that we want ?
             * wifi_prov_scheme_softap or wifi_prov_scheme_ble */
            .scheme = wifi_prov_scheme_softap,
            /* Any default scheme specific event handler that you would
             * like to choose. Since our example application requires
             * neither BT nor BLE, we can choose to release the associated
             * memory once provisioning is complete, or not needed
             * (in case when device is already provisioned). Choosing
             * appropriate scheme specific event handler allows the manager
             * to take care of this automatically. This can be set to
             * WIFI_PROV_EVENT_HANDLER_NONE when using wifi_prov_scheme_softap*/
            .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE,
        };

        /* Initialize provisioning manager with the
         * configuration parameters set above */
        ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

        ESP_LOGI(TAG, "Starting provisioning");
        esp_netif_create_default_wifi_ap();

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error: %d. Failed to get PoP from NVS, Please perform Claiming.", err);
            return err;
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_rmaker_user_mapping_endpoint_create failed %d", err);
            return err;
        }

        wifi_prov_mgr_endpoint_create("custom-data");

        /* Start provisioning service */
        ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_0, pop, ssid, password));

        /* The handler for the optional endpoint created above.
         * This call must be made after starting the provisioning, and only if the endpoint
         * has already been created above.
         */
        wifi_prov_mgr_endpoint_register("custom-data", esp_qcloud_prov_data_handler, NULL);
    }

    if ((SOFTAPCONFIG_TYPE_TENCENT & type) || (SOFTAPCONFIG_TYPE_ESPRESSIF_TENCENT & type)) {
        esp_qcloud_prov_udp_server_start();
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_prov_softapconfig_stop()
{
    wifi_prov_mgr_deinit();
    esp_qcloud_prov_udp_server_stop();

    return ESP_OK;
}
