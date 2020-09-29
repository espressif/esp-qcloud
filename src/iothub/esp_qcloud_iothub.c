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
#include <freertos/queue.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include "cJSON.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "mbedtls/error.h"

#include <esp_qcloud_iothub.h>
#include <esp_qcloud_utils.h>
#include "esp_qcloud_mqtt.h"
#include "esp_qcloud_utils.h"
#include "esp_qcloud_log.h"
#include "esp_qcloud_storage.h"

#define QCLOUD_IOTHUB_DEVICE_SDK_APPID             "21010406"
#define QCLOUD_IOTHUB_MQTT_DIRECT_DOMAIN           "iotcloud.tencentdevices.com"
#define QCLOUD_IOTHUB_MQTT_SERVER_PORT_TLS         8883
#define QCLOUD_IOTHUB_MQTT_SERVER_PORT_NOTLS       1883

ESP_EVENT_DEFINE_BASE(QCLOUD_EVENT);

typedef enum {
    IOTHUB_EVENT_BOND_RELAY        = BIT0,
    IOTHUB_EVENT_REPORT_INFO_REPLY = BIT1,
    IOTHUB_EVENT_GET_STATUS_REPLY  = BIT2,
    IOTHUB_EVENT_REPORT_REPLY      = BIT3,
} iot_hub_event_t;

typedef esp_err_t (*esp_qcloud_property_func_t)(const cJSON *request_data, cJSON *reply_data);

/**
 * @brief Type of handler list
 */
typedef struct {
    const char *down_method;                /**< The name of the function */
    esp_qcloud_property_func_t down_handle; /**< The pointer of the function */
    const char *up_method;                  /**< The name of the function */
    esp_qcloud_property_func_t up_handle;   /**< The pointer of the function */
} esp_qcloud_property_handle_t;

static const char *TAG = "esp_qcloud_iothub";
static EventGroupHandle_t g_iothub_group = NULL;
static bool g_qcloud_iothub_is_connected = false;
static const esp_qcloud_property_handle_t g_qcloud_property_handles[] = {
    {"report_info_reply", NULL, NULL, NULL},
    {"get_status_reply", NULL, NULL, NULL},
    {"report_reply", NULL, NULL, NULL},
    {"control", esp_qcloud_handle_set_param, "control_reply", NULL},
    {NULL, NULL, NULL, NULL},
};

bool esp_qcloud_iothub_is_connected()
{
    return g_qcloud_iothub_is_connected;
}

static esp_err_t esp_qcloud_iothub_subscribe(const char *topic, esp_qcloud_mqtt_subscribe_cb_t cb)
{
    esp_err_t err         = ESP_OK;
    char *subscribe_topic = NULL;

    asprintf(&subscribe_topic, "$thing/down/%s/%s/%s",
             topic, esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    err = esp_qcloud_mqtt_subscribe(subscribe_topic, cb, NULL);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> subscribe to %s", esp_err_to_name(err), subscribe_topic);

    ESP_LOGI(TAG, "mqtt_subscribe, topic: %s", subscribe_topic);


EXIT:
    ESP_QCLOUD_FREE(subscribe_topic);
    return err;
}

static esp_err_t esp_qcloud_iothub_publish(const char *topic, const char *method, cJSON *data)
{
    esp_err_t err       = ESP_FAIL;
    char *publish_topic = NULL;
    char *publish_data  = NULL;

    asprintf(&publish_topic, "$thing/up/%s/%s/%s",
             topic, esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

    cJSON *json_publish_data = cJSON_CreateObject();
    cJSON_AddStringToObject(json_publish_data, "method", method);

    char *token = NULL;
    asprintf(&token, "%s-%05u", esp_qcloud_get_device_name(), esp_random() % 100000);
    cJSON_AddStringToObject(json_publish_data, "clientToken", token);
    ESP_QCLOUD_FREE(token);

    if (data && data->child) {
        cJSON_AddItemToObject(json_publish_data, "params", data);
    }

    publish_data = cJSON_PrintUnformatted(json_publish_data);
    cJSON_Delete(json_publish_data);

    err = esp_qcloud_mqtt_publish(publish_topic, publish_data, strlen(publish_data));
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> Publish to %s, data: %s",
                          esp_err_to_name(err), publish_topic,  publish_data);

    ESP_LOGI(TAG, "mqtt_publish, topic: %s, method: %s, data: %s", publish_topic, method, publish_data);

EXIT:
    ESP_QCLOUD_FREE(publish_topic);
    ESP_QCLOUD_FREE(publish_data);

    return ESP_OK;
}

static esp_err_t esp_qcloud_iothub_reply(const char *method, const char *token, esp_err_t reply_code, cJSON *data)
{
    esp_err_t err = ESP_FAIL;
    char *publish_topic = NULL;
    char *publish_data = NULL;

    asprintf(&publish_topic, "$thing/up/property/%s/%s", esp_qcloud_get_product_id(), esp_qcloud_get_device_name());

    cJSON *json_publish_data = cJSON_CreateObject();
    cJSON_AddStringToObject(json_publish_data, "method", method);
    cJSON_AddNumberToObject(json_publish_data, "code", reply_code);
    cJSON_AddStringToObject(json_publish_data, "status", esp_err_to_name(reply_code));
    cJSON_AddStringToObject(json_publish_data, "clientToken", token);

    if (data->child) {
        cJSON_AddItemToObject(json_publish_data, "data", data);
    }

    publish_data = cJSON_PrintUnformatted(json_publish_data);
    cJSON_Delete(json_publish_data);

    err = esp_qcloud_mqtt_publish(publish_topic, publish_data, strlen(publish_data));
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> Publispublish, topic: $thing/up/property/%s/%s, data: h to %s, data: %s",
                          esp_err_to_name(err), esp_qcloud_get_product_id(), esp_qcloud_get_device_name(), publish_topic, publish_data);

    ESP_LOGI(TAG, "mqtt_publish, topic: %s, data: %s", publish_topic, publish_data);

EXIT:
    ESP_QCLOUD_FREE(publish_topic);
    ESP_QCLOUD_FREE(publish_data);

    return ESP_OK;
}

static esp_err_t esp_qcloud_iothub_config(esp_qcloud_mqtt_config_t *mqtt_cfg)
{
    esp_err_t err = ESP_FAIL;

#ifdef QCLOUD_MQTT_TRANSPORT_OVER_NOSSL
    asprintf(&mqtt_cfg->host, "mqtt://%s.%s:%d", esp_qcloud_get_product_id(),
             QCLOUD_IOTHUB_MQTT_DIRECT_DOMAIN, QCLOUD_IOTHUB_MQTT_SERVER_PORT_NOTLS);
#else
    asprintf(&mqtt_cfg->host, "mqtts://%s.%s:%d", esp_qcloud_get_product_id(),
             QCLOUD_IOTHUB_MQTT_DIRECT_DOMAIN, QCLOUD_IOTHUB_MQTT_SERVER_PORT_TLS);
#endif

    asprintf(&mqtt_cfg->client_id, "%s%s", esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    asprintf(&mqtt_cfg->username, "%s;%s;%05u;%ld", mqtt_cfg->client_id,
             QCLOUD_IOTHUB_DEVICE_SDK_APPID, esp_random() % 100000, LONG_MAX);

    switch (esp_qcloud_get_auth_mode()) {
        case QCLOUD_AUTH_MODE_KEY: {
            uint8_t digest[32] = {0};
            uint8_t base64_psk[48] = {0};
            size_t base64_len = 0;
            mbedtls_md_context_t sha_ctx;

            err = mbedtls_base64_decode(base64_psk, sizeof(base64_psk), &base64_len,
                                        (uint8_t *)esp_qcloud_get_device_secret(), strlen(esp_qcloud_get_device_secret()));
            ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_base64_decode");

            mbedtls_md_init(&sha_ctx);
            err = mbedtls_md_setup(&sha_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
            ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_setup");
            err = mbedtls_md_hmac_starts(&sha_ctx, base64_psk, base64_len);
            ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_hmac_starts");
            err = mbedtls_md_hmac_update(&sha_ctx, (uint8_t *)mqtt_cfg->username, strlen(mqtt_cfg->username));
            ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_hmac_update");
            err = mbedtls_md_hmac_finish(&sha_ctx, digest);
            ESP_QCLOUD_ERROR_GOTO(err != 0, EXIT, "mbedtls_md_hmac_finish");

            char *ptr_hd = mqtt_cfg->password = ESP_QCLOUD_CALLOC(1, 128);

            for (int i = 0; i < sizeof(digest); i++) {
                ptr_hd += sprintf(ptr_hd, "%02x", digest[i]);
            }

            strcat(mqtt_cfg->password, ";hmacsha256");

EXIT:

            if (err != 0) {
                char err_buf[128] = {0};
                mbedtls_strerror(err, err_buf, sizeof(err_buf) - 1);
                ESP_LOGW(TAG, "%s", err_buf);
            }

            mbedtls_md_free(&sha_ctx);
            break;
        }

        case QCLOUD_AUTH_MODE_CERT:
            mqtt_cfg->client_cert = (char *)esp_qcloud_get_cert_crt();
            mqtt_cfg->client_key  = (char *)esp_qcloud_get_private_key();
            break;

        default:
            ESP_LOGE(TAG, "Does not support this authentication method, auth_mode: %d", esp_qcloud_get_auth_mode());
            break;
    }

    return err;
}

static void esp_qcloud_iothub_property_callback(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    ESP_LOGI(TAG, "property_callback, topic: %s, payload: %.*s", topic, payload_len, (char *)payload);

    esp_err_t err = ESP_FAIL;
    cJSON *reply_data = cJSON_CreateObject();
    cJSON *request_data  = cJSON_Parse(payload);
    ESP_QCLOUD_ERROR_GOTO(!request_data, EXIT, "The data format is wrong and cannot be parsed");

    const char *client_token = cJSON_GetObjectItem(request_data, "clientToken")->valuestring;
    ESP_QCLOUD_ERROR_GOTO(!client_token, EXIT, "The data format is wrong, the 'clientToken' field is not included");

    const char *method = cJSON_GetObjectItem(request_data, "method")->valuestring;

    /**< If we can find this request from our list, we will handle this request */
    for (int i = 0; g_qcloud_property_handles[i].down_method; i++) {
        if (!strcasecmp(method, g_qcloud_property_handles[i].down_method)) {
            ESP_LOGD(TAG, "method: %s", method);

            if (g_qcloud_property_handles[i].down_handle) {
                cJSON *request_params = cJSON_GetObjectItem(request_data, "params");
                ESP_QCLOUD_ERROR_GOTO(!request_params, EXIT, "The data format is wrong, the 'params' field is not included");

                err = g_qcloud_property_handles[i].down_handle(request_params, reply_data);
            }

            if (g_qcloud_property_handles[i].up_method) {
                esp_qcloud_iothub_reply(g_qcloud_property_handles[i].up_method, client_token, err, reply_data);
            }

            break;
        }
    }

EXIT:
    cJSON_Delete(request_data);
    cJSON_Delete(reply_data);
}

static esp_err_t esp_qcloud_iothub_register_property()
{
    esp_err_t err = ESP_FAIL;

    err = esp_qcloud_iothub_subscribe("property", esp_qcloud_iothub_property_callback);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iotbub_subscribe");

    return err;
}

esp_err_t esp_qcloud_iothub_report_property()
{
    esp_err_t err = ESP_FAIL;
    cJSON *params_json = cJSON_CreateObject();

    err = esp_qcloud_handle_get_param(NULL, params_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_register_get_params", esp_err_to_name(err));

    err = esp_qcloud_iothub_publish("property", "report", params_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_register_get_params", esp_err_to_name(err));

    return ESP_OK;
}

static void esp_qcloud_iothub_log_callback(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    ESP_LOGI(TAG, "log_callback, topic: %s, payload: %.*s", topic, payload_len, (char *)payload);

    cJSON *request_data = cJSON_Parse(payload);
    ESP_QCLOUD_ERROR_GOTO(!request_data, EXIT, "The data format is wrong and cannot be parsed");

    if (!strcasecmp(cJSON_GetObjectItem(request_data, "type")->valuestring, "get_log_level")
            && cJSON_GetObjectItem(request_data, "log_level")) {
        esp_qcloud_log_config_t config = {0};
        esp_qcloud_log_get_config(&config);

        config.log_level_iothub = cJSON_GetObjectItem(request_data, "log_level")->valueint;
        ESP_LOGI(TAG, "log_level: %d", config.log_level_iothub);

        esp_qcloud_log_set_config(&config);
    }

EXIT:
    cJSON_Delete(request_data);
}

/* Enable the ESP QCloud specific OTA */
static esp_err_t esp_qcloud_iothub_register_log()
{
    esp_err_t err         = ESP_OK;
    char *publish_topic   = NULL;
    char *publish_data    = NULL;
    char *subscribe_topic = NULL;

    /**
     * @brief ubscribed server firmware upgrade news
     */
    asprintf(&subscribe_topic, "$log/operation/result/%s/%s",
             esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    err = esp_qcloud_mqtt_subscribe(subscribe_topic, esp_qcloud_iothub_log_callback, NULL);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> subscribe to %s", esp_err_to_name(err), subscribe_topic);

    ESP_LOGI(TAG, "mqtt_subscribe, topic: %s", subscribe_topic);

    /**
     * @breif The device reports the current version number
     */
    asprintf(&publish_topic, "$log/operation/%s/%s",
             esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    asprintf(&publish_data, "{\"type\":\"get_log_level\",\"clientToken\": \"%s-%05u\"}", esp_qcloud_get_product_id(), esp_random() % 100000);
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

esp_err_t esp_qcloud_iothub_init()
{
    esp_err_t err = ESP_FAIL;
    esp_qcloud_mqtt_config_t mqtt_cfg = {0};

    g_iothub_group = xEventGroupCreate();

    err = esp_qcloud_iothub_config(&mqtt_cfg);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_mqtt_get_config");

    err = esp_qcloud_mqtt_init(&mqtt_cfg);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_mqtt_init");

    ESP_LOGD(TAG, "QCloud iothub mqtt config:");
    ESP_LOGD(TAG, "qcloud_uri: %s", mqtt_cfg.host);
    ESP_LOGD(TAG, "client_id: %s", mqtt_cfg.client_id);
    ESP_LOGD(TAG, "username: %s", mqtt_cfg.username);
    ESP_LOGD(TAG, "password: %s", mqtt_cfg.password);

    err = esp_qcloud_mqtt_connect();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_mqtt_connect");

    esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_INIT_DONE, NULL, 0, portMAX_DELAY);

    return ESP_OK;
}

static void esp_qcloud_iothub_bond_callback(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    ESP_LOGI(TAG, "bond_callback: topic: %s, payload: %.*s", topic, payload_len, (char *)payload);

    cJSON *root_json  = cJSON_Parse(payload);
    ESP_QCLOUD_ERROR_GOTO(!root_json, EXIT, "The data format is wrong and cannot be parsed");

    const char *method = cJSON_GetObjectItem(root_json, "method")->valuestring;
    ESP_QCLOUD_ERROR_GOTO(!method, EXIT, "The data format is wrong, the 'clientToken' field is not included");

    if (!strcmp(method, "unbind_device")) {
        esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_UNBOND_DEVICE, NULL, 0, portMAX_DELAY);
    } else if (!strcmp(method, "app_bind_token_reply")) {
        xEventGroupSetBits(g_iothub_group, IOTHUB_EVENT_BOND_RELAY);
        esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_BOND_DEVICE, NULL, 0, portMAX_DELAY);
        esp_qcloud_storage_erase("token");
    }

EXIT:
    cJSON_Delete(root_json);
}

esp_err_t esp_qcloud_iothub_bind(const char *token)
{
    esp_err_t err = ESP_FAIL;
    cJSON *data_json = NULL;

    err = esp_qcloud_iothub_subscribe("service", esp_qcloud_iothub_bond_callback);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_subscribe");

    data_json = cJSON_CreateObject();
    cJSON_AddStringToObject(data_json, "token", token);

#ifdef ESP_QCLOUD_IOTHUB_BIND_RETRY

    for (size_t i = 0; i < 3; i++) {
        err = esp_qcloud_iothub_publish("service", "app_bind_token", data_json);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_OK, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));

        if (xEventGroupWaitBits(g_iothub_group, IOTHUB_EVENT_BOND_RELAY, false, true, pdMS_TO_TICKS(3 * 1000))) {
            break;
        }
    }

#else
    err = esp_qcloud_iothub_publish("service", "app_bind_token", data_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));
#endif

    data_json = cJSON_CreateObject();
    cJSON_AddStringToObject(data_json, "module_hardinfo", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(data_json, "module_softinfo", esp_get_idf_version());
    cJSON_AddStringToObject(data_json, "fw_ver", esp_qcloud_get_version());
    err = esp_qcloud_iothub_publish("property", "report_info", data_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));

    return err;
}

esp_err_t esp_qcloud_iothub_start()
{
    esp_err_t err = ESP_FAIL;
    char token[AUTH_TOKEN_MAX_SIZE] = {0};

    if (esp_qcloud_storage_get("token", token, AUTH_TOKEN_MAX_SIZE) == ESP_OK) {
        esp_qcloud_iothub_bind(token);
    }

    err = esp_qcloud_iothub_register_log();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_report_property");

    err = esp_qcloud_iothub_register_property();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_register_property");

    err = esp_qcloud_iothub_report_property();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_report_property");

    g_qcloud_iothub_is_connected = true;

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_stop()
{
    g_qcloud_iothub_is_connected = false;
    esp_qcloud_mqtt_disconnect();
    return ESP_OK;
}
