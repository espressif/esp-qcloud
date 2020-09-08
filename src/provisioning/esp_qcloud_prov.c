/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <wifi_provisioning/manager.h>
#ifdef CONFIG_APP_WIFI_PROV_TRANSPORT_BLE
#include <wifi_provisioning/scheme_ble.h>
#else /* CONFIG_APP_WIFI_PROV_TRANSPORT_SOFTAP */
#include <wifi_provisioning/scheme_softap.h>
#endif /* CONFIG_APP_WIFI_PROV_TRANSPORT_BLE */

// #include <esp_qcloud_user_mapping.h>
#include <qrcode.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "esp_qcloud_prov.h"
#include "cJSON.h"
#include "esp_qcloud_storage.h"
#include "esp_smartconfig.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_qcloud_iothub.h"

#define PROV_QR_VERSION             "v1"

#define PROV_TRANSPORT_SOFTAP       "softap"
#define PROV_TRANSPORT_BLE          "ble"
#define PROV_TRANSPORT_SMARTCONFIG  "smartconfig"
#define QRCODE_BASE_URL             "https://rainmaker.espressif.com/qrcode.html"

#define APP_SERVER_PORT 8266

typedef enum {
    CMD_TOKEN_ONLY = 0,           /* Token only for smartconfig  */
    CMD_SSID_PW_TOKEN = 1,        /* SSID/PW/TOKEN for softAP */
    CMD_DEVICE_REPLY = 2,         /* device reply */
} type_wifi_config_t;

typedef enum {
    QCLOUD_PROV_EVENT_STA_CONNECTED = BIT0,
    QCLOUD_PROV_EVENT_ESPTOUCH_DONE = BIT1,
    QCLOUD_PROV_EVENT_GET_TOKEN     = BIT2,
    QCLOUD_PROV_EVENT_AP_STARED     = BIT3,
} esp_qcloud_prov_event_t;

static const char *TAG  = "esp_qcloud_prov";
static char *g_token    = NULL;
static EventGroupHandle_t s_wifi_event_group;
static int g_udp_server_sockfd = -1;

static void esp_qcloud_wifi_print_qr(const char *name, const char *pop, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }

    char *payload = NULL;

    asprintf(&payload, "https://iot.cloud.tencent.com/iotexplorer/device?page=%s&productId=%s&ver=%s&name=%s&pop=%s",
             transport, esp_qcloud_get_product_id(), PROV_QR_VERSION, name, pop ? pop : "");
    ESP_LOGI(TAG, "Scan this QR code from the Wechat for Provisioning.");
    qrcode_display(payload);
    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\n%s?data=%s", QRCODE_BASE_URL, payload);

    ESP_QCLOUD_FREE(payload);
}

/* Event handler for catching system events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
        /* Signal main application to continue execution */
        xEventGroupSetBits(s_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Connecting to the AP again...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "STA Connecting to the AP again...");
        xEventGroupSetBits(s_wifi_event_group, QCLOUD_PROV_EVENT_AP_STARED);
    }
}

esp_err_t esp_qcloud_wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    return ESP_OK;
}

esp_err_t esp_qcloud_wifi_start(const wifi_config_t *conf)
{
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, (wifi_config_t *)conf));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(s_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED, true, true, portMAX_DELAY);

    return ESP_OK;
}

#define UDP_SERVER_BUFFER_MAX_SIZE 128

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

    g_udp_server_sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    ESP_QCLOUD_ERROR_GOTO(g_udp_server_sockfd < 0, EXIT, "Unable to create socket, errno %d, err_str: %s", errno, strerror(errno));

    err = bind(g_udp_server_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    ESP_QCLOUD_ERROR_GOTO(err < 0, EXIT, "Socket unable to bind: errno %d, err_str: %s", errno, strerror(errno));

    ESP_LOGI(TAG, "Socket bound, port %d", APP_SERVER_PORT);

    int len = 0;

    for (;;) {
        memset(rx_buffer, 0, UDP_SERVER_BUFFER_MAX_SIZE);
        len = recvfrom(g_udp_server_sockfd, rx_buffer, UDP_SERVER_BUFFER_MAX_SIZE, 0, (struct sockaddr *)&client_addr, &socklen);
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

        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED, false, true, pdMS_TO_TICKS(5 * 1000));

        if (!(bits & QCLOUD_PROV_EVENT_STA_CONNECTED)) {
            wifi_config_t wifi_cfg = {0};
            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_cfg));
            ESP_ERROR_CHECK(esp_wifi_disconnect());
            ESP_LOGW(TAG, "Timeout waiting for connection router, please try again");
            continue;
        }

        xEventGroupSetBits(s_wifi_event_group, QCLOUD_PROV_EVENT_GET_TOKEN);

        char *tx_buffer = NULL;
        asprintf(&tx_buffer, "{\"cmdType\":%d,\"productId\":\"%s\",\"deviceName\":\"%s\",\"protoVersion\":\"2.0\"}",
                 CMD_DEVICE_REPLY, esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

        ESP_LOGI(TAG, "sendto, data: %s", tx_buffer);

        for (int i = 0; i < 5; i++) {
            err = sendto(g_udp_server_sockfd, tx_buffer, strlen(tx_buffer), 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
            ESP_QCLOUD_ERROR_BREAK(err < 0, "sendto failed, errno %d, err_str: %s", errno, strerror(errno));

            vTaskDelay(pdMS_TO_TICKS(i * 10));
        }

        ESP_QCLOUD_FREE(tx_buffer);

        break;
    }

EXIT:

    if (g_udp_server_sockfd != -1) {
        ESP_LOGI(TAG, "Shutting down socket");
        shutdown(g_udp_server_sockfd, 0);
        close(g_udp_server_sockfd);
    }

    ESP_QCLOUD_FREE(rx_buffer);
    vTaskDelete(NULL);
}

/* Event handler for catching system events */
static void smartconfig_event_handler(void *arg, esp_event_base_t event_base,
                                      int event_id, void *event_data)
{
    switch (event_id) {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "Scan done");
            break;

        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "Found channel");
            break;

        case SC_EVENT_GOT_SSID_PSWD: {
            wifi_config_t wifi_config = {0};
            smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

            ESP_LOGI(TAG, "Got SSID and password, ssid: %s, password: %s", evt->ssid, evt->password);
            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

            ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_connect());
            break;
        }

        case SC_EVENT_SEND_ACK_DONE :
            xEventGroupSetBits(s_wifi_event_group, QCLOUD_PROV_EVENT_ESPTOUCH_DONE);
            break;

        default:
            break;
    }
}

esp_err_t esp_qcloud_prov_smartconfig_start()
{
    esp_err_t err = ESP_OK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    // ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH_AIRKISS));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &smartconfig_event_handler, NULL));
    esp_qcloud_wifi_print_qr("NULL", NULL, PROV_TRANSPORT_SOFTAP);

    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);

    return err;
}

esp_err_t esp_qcloud_prov_smartconfig_stop()
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "smartconfig over");
    esp_smartconfig_stop();

    if (g_udp_server_sockfd != -1) {
        ESP_LOGI(TAG, "Shutting down socket");
        shutdown(g_udp_server_sockfd, 0);
        close(g_udp_server_sockfd);
    }

    return err;
}

static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int event_id, void *event_data)
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

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii text.
 * Applications can choose to use other formats like protobuf, JSON, XML, etc.
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                   uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }

    cJSON *root = cJSON_Parse((char *)inbuf);
    cJSON *token_json = cJSON_GetObjectItem(root, "token");
    g_token = strdup(token_json->valuestring);

    *outlen = asprintf((char **)outbuf, "{\"cmdType\":2,\"productId\":\"%s\",\"deviceName\":\"%s\",\"protoVersion\":\"2.0\"}",
                       esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

    cJSON_Delete(root);

    xEventGroupSetBits(s_wifi_event_group, QCLOUD_PROV_EVENT_GET_TOKEN);

    return ESP_OK;
}
esp_err_t esp_qcloud_prov_softap_start(const char *ssid, const char *password, const char *pop)
{
    esp_err_t err = ESP_OK;

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
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, pop, ssid, password));

    /* The handler for the optional endpoint created above.
     * This call must be made after starting the provisioning, and only if the endpoint
     * has already been created above.
     */
    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

    /* Print QR code for provisioning */
    esp_qcloud_wifi_print_qr(ssid, pop, PROV_TRANSPORT_SOFTAP);
    ESP_LOGI(TAG, "Provisioning Started. Name: %s, POP: %s", ssid, pop ? pop : "");

    xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, NULL);


    return ESP_OK;
}

esp_err_t esp_qcloud_prov_softap_stop()
{
    wifi_prov_mgr_deinit();

    if (g_udp_server_sockfd != -1) {
        ESP_LOGI(TAG, "Shutting down socket");
        shutdown(g_udp_server_sockfd, 0);
        close(g_udp_server_sockfd);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_prov_wait(wifi_config_t **sta_cfg, char **token, TickType_t ticks_wait)
{
    /* Wait for Wi-Fi connection */
    xEventGroupWaitBits(s_wifi_event_group, QCLOUD_PROV_EVENT_STA_CONNECTED | QCLOUD_PROV_EVENT_GET_TOKEN,
                        false, true, ticks_wait);

    *token   = g_token;
    *sta_cfg = ESP_QCLOUD_MALLOC(sizeof(wifi_config_t));
    esp_wifi_get_config(ESP_IF_WIFI_STA, *sta_cfg);

    return ESP_OK;
}
