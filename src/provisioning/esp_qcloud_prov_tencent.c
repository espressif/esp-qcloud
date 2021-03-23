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
#include <freertos/event_groups.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <cJSON.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include "esp_smartconfig.h"

#ifdef CONFIG_LIGHT_PROVISIONING_BLECONFIG
#include "esp_blufi_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#endif

#include <wifi_provisioning/manager.h>
#ifdef CONFIG_APP_WIFI_PROV_TRANSPORT_BLE
#include <wifi_provisioning/scheme_ble.h>
#else /* CONFIG_APP_WIFI_PROV_TRANSPORT_SOFTAP */
#include <wifi_provisioning/scheme_softap.h>
#endif /* CONFIG_APP_WIFI_PROV_TRANSPORT_BLE */


#include "esp_qcloud_prov.h"
#include "esp_qcloud_storage.h"
#include "esp_qcloud_iothub.h"
#include "esp_qcloud_prov.h"
#include <qrcode.h>
#include "esp_qcloud_prov_tencent.h"

#define PROV_QR_VERSION            "v1"
#define APP_SERVER_PORT            8266
#define UDP_SERVER_BUFFER_MAX_SIZE 128

typedef enum {
    QCLOUD_PROV_EVENT_STA_CONNECTED = BIT0,
    QCLOUD_PROV_EVENT_ESPTOUCH_DONE = BIT1,
    QCLOUD_PROV_EVENT_GET_TOKEN     = BIT2,
} esp_qcloud_prov_event_t;

static const char *TAG = "esp_qcloud_prov";
static char *g_token   = NULL;
static EventGroupHandle_t g_wifi_event_group;
static bool g_prov_server_start_flag = false;


/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));

#ifdef CONFIG_LIGHT_PROVISIONING_BLECONFIG
        wifi_mode_t mode;
        esp_blufi_extra_info_t info = { 0 };
        wifi_config_t sta_cfg   = { 0 };

        esp_wifi_get_mode(&mode);
        esp_wifi_get_config(ESP_IF_WIFI_STA, &sta_cfg);

        info.sta_bssid_set = false;
        memcpy(info.sta_bssid, sta_cfg.sta.bssid, 6);
        info.sta_ssid_len = strlen((char*)(sta_cfg.sta.ssid));
        info.sta_ssid = sta_cfg.sta.ssid;
  
        if(esp_qcloud_prov_ble_is_connected()){
            ESP_LOGI(TAG, "info SSID: %s Len: %d", info.sta_ssid , info.sta_ssid_len);
            esp_blufi_send_wifi_conn_report(mode, ESP_BLUFI_STA_CONN_SUCCESS, 0, &info);
        }
#endif

        /* Signal main application to continue execution */
        xEventGroupSetBits(g_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "STA Connecting to the AP again...");
        esp_qcloud_prov_smartconfig_stop();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "Disconnect reason : %d", disconnected->reason);
        if(disconnected->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT){

#ifdef CONFIG_LIGHT_PROVISIONING_BLECONFIG
        esp_blufi_send_wifi_conn_report(WIFI_MODE_STA, ESP_BLUFI_STA_CONN_FAIL, 0, NULL);
#endif

        }
    }
}

void esp_qcloud_prov_print_wechat_qr(const char *name, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }

    char *qcloud_payload = NULL;
    char *terminal_payload = NULL;

    asprintf(&terminal_payload, "https://iot.cloud.tencent.com/iotexplorer/device?page=%s&productId=%s&ver=%s&name=%s",
             transport, esp_qcloud_get_product_id(), PROV_QR_VERSION, name);
    ESP_LOGI(TAG, "Scan this QR code from the Wechat for Provisioning.");
    qrcode_display(terminal_payload);
    
    asprintf(&qcloud_payload, "https://iot.cloud.tencent.com/iotexplorer/device?page=%s%%26productId=%s%%26ver=%s%%26name=%s",
             transport, esp_qcloud_get_product_id(), PROV_QR_VERSION, name);
    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s",
             "https://rainmaker.espressif.com/qrcode.html", qcloud_payload);

    ESP_QCLOUD_FREE(qcloud_payload);
    ESP_QCLOUD_FREE(terminal_payload);
}

esp_err_t esp_qcloud_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                       uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }

    cJSON *root = cJSON_Parse((char *)inbuf);
    ESP_QCLOUD_ERROR_GOTO(!root, EXIT, "The data format is wrong");

    cJSON *token_json = cJSON_GetObjectItem(root, "token");
    ESP_QCLOUD_ERROR_GOTO(!token_json, EXIT, "The data format is wrong, the 'token' field is not included");

    g_token = strdup(token_json->valuestring);

    *outlen = asprintf((char **)outbuf,
                       "{\"cmdType\":2,\"productId\":\"%s\",\"deviceName\":\"%s\",\"protoVersion\":\"2.0\"}",
                       esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

    xEventGroupSetBits(g_wifi_event_group, QCLOUD_PROV_EVENT_GET_TOKEN);

EXIT:
    cJSON_Delete(root);
    return ESP_OK;
}

static void udp_server_task(void *pvParameters)
{
    esp_err_t err     = ESP_FAIL;
    char *rx_buffer   = ESP_QCLOUD_MALLOC(UDP_SERVER_BUFFER_MAX_SIZE);
    socklen_t socklen = sizeof(struct sockaddr_in);
    struct sockaddr_in client_addr = {0};
    struct sockaddr_in server_addr = {
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_family      = AF_INET,
        .sin_port        = htons(APP_SERVER_PORT),
    };

    int udp_server_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    ESP_QCLOUD_ERROR_GOTO(udp_server_sockfd < 0, EXIT, "Unable to create socket, errno %d, err_str: %s", errno, strerror(errno));

    err = bind(udp_server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ESP_QCLOUD_ERROR_GOTO(err < 0, EXIT, "Socket unable to bind: errno %d, err_str: %s", errno, strerror(errno));

    ESP_LOGI(TAG, "Socket bound, port %d", APP_SERVER_PORT);

    while (g_prov_server_start_flag) {
        fd_set read_fd;
        struct timeval timeout = {.tv_sec = 1,};
        FD_ZERO(&read_fd);
        FD_SET(udp_server_sockfd, &read_fd);
        err = select(udp_server_sockfd + 1, &read_fd, NULL, NULL, &timeout);
        ESP_QCLOUD_ERROR_GOTO(err < 0, EXIT, "recvfrom failed, errno %d, err_str: %s", errno, strerror(errno));
        ESP_QCLOUD_ERROR_CONTINUE(err == 0 || !FD_ISSET(udp_server_sockfd, &read_fd), "");

        memset(rx_buffer, 0, UDP_SERVER_BUFFER_MAX_SIZE);
        int len = recvfrom(udp_server_sockfd, rx_buffer, UDP_SERVER_BUFFER_MAX_SIZE, 0, (struct sockaddr *)&client_addr, &socklen);
        ESP_QCLOUD_ERROR_GOTO(len < 0, EXIT, "recvfrom failed, errno %d, err_str: %s", errno, strerror(errno));

        ESP_LOGI(TAG, "recvfrom, data: %s", rx_buffer);

        cJSON *json_root = cJSON_Parse(rx_buffer);
        ESP_QCLOUD_ERROR_CONTINUE(!json_root, "cJSON_Parse failed, recv data: %.*s", len, rx_buffer);

        g_token = strdup(cJSON_GetObjectItem(json_root, "token")->valuestring);

        if (cJSON_GetObjectItem(json_root, "cmdType")->valueint == CMD_SSID_PW_TOKEN) {
            wifi_config_t wifi_cfg = {0};
            strcpy((char *)wifi_cfg.sta.ssid, cJSON_GetObjectItem(json_root, "ssid")->valuestring);
            strcpy((char *)wifi_cfg.sta.password, cJSON_GetObjectItem(json_root, "password")->valuestring);

            // /* Configure Wi-Fi as both AP and/or Station */
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
            ESP_ERROR_CHECK(esp_wifi_connect());
        }

        cJSON_Delete(json_root);

        EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED, false, true, pdMS_TO_TICKS(5 * 1000));

        if (!(bits & QCLOUD_PROV_EVENT_STA_CONNECTED)) {
            wifi_config_t wifi_cfg = {0};
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_LOGW(TAG, "Timeout waiting for connection router, please try again");
            continue;
        }

        xEventGroupSetBits(g_wifi_event_group, QCLOUD_PROV_EVENT_GET_TOKEN);

        char *tx_buffer = NULL;
        asprintf(&tx_buffer, "{\"cmdType\":%d,\"productId\":\"%s\",\"deviceName\":\"%s\",\"protoVersion\":\"2.0\"}",
                 CMD_DEVICE_REPLY, esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

        ESP_LOGI(TAG, "sendto, data: %s", tx_buffer);

        for (int i = 0; i < 5; i++) {
            vTaskDelay(pdMS_TO_TICKS(i * 10));
            err = sendto(udp_server_sockfd, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
            ESP_QCLOUD_ERROR_CONTINUE(err < 0, "sendto failed, errno %d, err_str: %s", errno, strerror(errno));
            
            break;
        }

        ESP_QCLOUD_FREE(tx_buffer);

        break;
    }

EXIT:

    if (udp_server_sockfd != -1) {
        ESP_LOGI(TAG, "Shutting down socket");
        shutdown(udp_server_sockfd, 0);
        close(udp_server_sockfd);
    }

    ESP_QCLOUD_FREE(rx_buffer);
    vTaskDelete(NULL);
}

esp_err_t esp_qcloud_prov_udp_server_start()
{
    if (!g_prov_server_start_flag) {
        g_prov_server_start_flag = true;
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

#if (CONFIG_LIGHT_PROVISIONING_SMARTCONFIG) || (CONFIG_LIGHT_PROVISIONING_SOFTAPCONFIG)
        xTaskCreate(udp_server_task, "prov_udp_server", 4096, NULL, 5, NULL);
#endif
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_prov_udp_server_stop()
{
    g_prov_server_start_flag = false;

    return ESP_OK;
}

esp_err_t esp_qcloud_prov_ble_set_token(char *token, size_t token_len)
{
    g_token = strdup(token);
    xEventGroupSetBits(g_wifi_event_group, QCLOUD_PROV_EVENT_GET_TOKEN);

    return ESP_OK;
}

esp_err_t esp_qcloud_prov_wait(wifi_config_t *sta_cfg, uint32_t wait_ms)
{
    g_wifi_event_group = xEventGroupCreate();

    /* Wait for Wi-Fi connection */
     EventBits_t bits = xEventGroupWaitBits(g_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED | QCLOUD_PROV_EVENT_GET_TOKEN,
                        false, true, pdMS_TO_TICKS(wait_ms));
    if ((bits & QCLOUD_PROV_EVENT_GET_TOKEN) && (bits & QCLOUD_PROV_EVENT_STA_CONNECTED) ) {
        esp_qcloud_storage_set("token", g_token, AUTH_TOKEN_MAX_SIZE);
        esp_wifi_get_config(ESP_IF_WIFI_STA, sta_cfg);

        return ESP_OK;
    }

    return ESP_FAIL;
}
