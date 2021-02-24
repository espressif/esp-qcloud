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
#include "esp_qcloud_prov.h"

#define QCLOUD_IOTHUB_DEVICE_SDK_APPID             "21010406"
#define QCLOUD_IOTHUB_MQTT_DIRECT_DOMAIN           "iotcloud.tencentdevices.com"
#define QCLOUD_IOTHUB_MQTT_SERVER_PORT_TLS         8883
#define QCLOUD_IOTHUB_MQTT_SERVER_PORT_NOTLS       1883

#define QCLOUD_IOTHUB_BINDING_TIMEOUT              40000  
#define EVENT_VERSION                              "1.0"
#define TOPIC_METHOD_NAME_MAX_SIZE                 16

#ifdef CONFIG_AUTH_MODE_CERT
extern const uint8_t qcloud_root_cert_crt_start[] asm("_binary_qcloud_root_cert_crt_start");
extern const uint8_t qcloud_root_cert_crt_end[] asm("_binary_qcloud_root_cert_crt_end");
#endif

ESP_EVENT_DEFINE_BASE(QCLOUD_EVENT);

typedef enum {
    IOTHUB_EVENT_BOND_RELAY        = BIT0,
    IOTHUB_EVENT_BIND_SUCCESS      = BIT1,
    IOTHUB_EVENT_BIND_FAIL         = BIT2,
    IOTHUB_EVENT_REPORT_INFO_REPLY = BIT3,
    IOTHUB_EVENT_GET_STATUS_REPLY  = BIT4,
    IOTHUB_EVENT_REPORT_REPLY      = BIT5,
} iot_hub_event_t;

static const char *TAG = "esp_qcloud_iothub";
static EventGroupHandle_t g_iothub_group = NULL;
static bool g_qcloud_iothub_is_connected = false;
static bool g_get_status_need_update     = false;

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

static esp_err_t esp_qcloud_iothub_publish(const char *topic, const char *method, esp_qcloud_method_extra_val_t *extra_val, cJSON *data)
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
        cJSON_AddItemReferenceToObject(json_publish_data, "params", data);
    }

    if(extra_val){
        if(!strcmp(method, "report")){
            cJSON_AddNumberToObject(json_publish_data, "timestamp", extra_val->timestamp);
        } else if(!strcmp(method, "event_post")){
            cJSON_AddStringToObject(json_publish_data, "type", extra_val->type);
            cJSON_AddStringToObject(json_publish_data, "eventId", extra_val->id);
            cJSON_AddStringToObject(json_publish_data, "version", extra_val->version);
            cJSON_AddNumberToObject(json_publish_data, "timestamp", extra_val->timestamp);
        }  else if(!strcmp(method, "action_reply")) {
            cJSON_AddNumberToObject(json_publish_data, "code", extra_val->code);
            cJSON_AddStringToObject(json_publish_data, "status", esp_err_to_name(extra_val->code));
            cJSON_DeleteItemFromObject(json_publish_data, "clientToken");
            cJSON_AddStringToObject(json_publish_data, "clientToken", extra_val->token);
            if (data && data->child) {
                cJSON_DeleteItemFromObject(json_publish_data, "params");
                cJSON_AddItemReferenceToObject(json_publish_data, "response", data);
            }
        }
    }

    publish_data = cJSON_PrintUnformatted(json_publish_data);
    ESP_LOGI(TAG, "publish_data:%s", publish_data);
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
    esp_err_t err = ESP_OK;

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

            asprintf(&mqtt_cfg->host, "mqtt://%s.%s:%d", esp_qcloud_get_product_id(),
             QCLOUD_IOTHUB_MQTT_DIRECT_DOMAIN, QCLOUD_IOTHUB_MQTT_SERVER_PORT_NOTLS);

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

#ifdef CONFIG_AUTH_MODE_CERT
            mqtt_cfg->server_cert = (char *)qcloud_root_cert_crt_start;
#endif
            /* When using certificate authentication, Qcloud does not verify the password */
            mqtt_cfg->password = QCLOUD_IOTHUB_DEVICE_SDK_APPID;

            asprintf(&mqtt_cfg->host, "mqtts://%s.%s:%d", esp_qcloud_get_product_id(),
             QCLOUD_IOTHUB_MQTT_DIRECT_DOMAIN, QCLOUD_IOTHUB_MQTT_SERVER_PORT_TLS);
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

    if(!strcmp(method, "control")) {
        cJSON *request_params = cJSON_GetObjectItem(request_data, "params");
        err = esp_qcloud_handle_set_param(request_params, reply_data);
        esp_qcloud_iothub_reply("control_reply", client_token, err, reply_data);
    } else if(!strcmp(method, "get_status_reply")){
        const int result_code = cJSON_GetObjectItem(request_data, "code")->valueint;
        if(0 == result_code) {
            cJSON *data = cJSON_GetObjectItem(request_data, "data");
            ESP_QCLOUD_ERROR_GOTO(!data, EXIT, "The data format is wrong, the 'data' field is not included");
            cJSON *reported = cJSON_GetObjectItem(data, "reported");
            char *reported_str = cJSON_PrintUnformatted(reported);
            if(g_get_status_need_update){
                esp_qcloud_handle_set_param(reported, reply_data);
            }
            /*Need to pass true length (including '/0')*/
            esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_RECEIVE_STATUS, reported_str, strlen(reported_str) + 1, portMAX_DELAY);
            ESP_QCLOUD_FREE(reported_str);
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

esp_err_t esp_qcloud_iothub_report_all_property(void)
{
    esp_err_t err = ESP_FAIL;
    cJSON *params_json = cJSON_CreateObject();

    err = esp_qcloud_handle_get_param(NULL, params_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_register_get_params", esp_err_to_name(err));

    err = esp_qcloud_iothub_publish("property", "report", NULL, params_json);
    cJSON_Delete(params_json);
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

static esp_err_t esp_qcloud_iothub_register_log()
{
    esp_err_t err         = ESP_OK;
    char *publish_topic   = NULL;
    char *publish_data    = NULL;
    char *subscribe_topic = NULL;

    asprintf(&subscribe_topic, "$log/operation/result/%s/%s",
             esp_qcloud_get_product_id(), esp_qcloud_get_device_name());
    err = esp_qcloud_mqtt_subscribe(subscribe_topic, esp_qcloud_iothub_log_callback, NULL);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> subscribe to %s", esp_err_to_name(err), subscribe_topic);

    ESP_LOGI(TAG, "mqtt_subscribe, topic: %s", subscribe_topic);

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
        const int result_code = cJSON_GetObjectItem(root_json, "code")->valueint;
        esp_qcloud_storage_erase("token");

#ifdef CONFIG_LIGHT_PROVISIONING_BLECONFIG
           /* This is an asynchronous operation, need to wait for the data to be sent */          
            esp_qcloud_prov_ble_report_bind_status(result_code);
            vTaskDelay(10);
            esp_qcloud_prov_ble_stop();
#endif

        if(0 == result_code){
            ESP_LOGI(TAG, "Successfully bind to the cloud");
            ESP_LOGW(TAG, "Waiting to be bound to the family");
        } else {
            xEventGroupSetBits(g_iothub_group, IOTHUB_EVENT_BIND_FAIL);
            esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_BIND_EXCEPTION, NULL, 0, portMAX_DELAY);
        }
    } else if (!strcmp(method, "bind_device")) {
        ESP_LOGI(TAG, "Successfully bound to the family");
        xEventGroupSetBits(g_iothub_group, IOTHUB_EVENT_BIND_SUCCESS);
        esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_BOND_DEVICE, NULL, 0, portMAX_DELAY);
    }

EXIT:
    cJSON_Delete(root_json);
}

static esp_err_t esp_qcloud_iothub_register_service()
{
    esp_err_t err = ESP_FAIL;

    err = esp_qcloud_iothub_subscribe("service", esp_qcloud_iothub_bond_callback);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_subscribe");

    return err;
}

esp_err_t esp_qcloud_iothub_bind(const char *token, bool block)
{
    esp_err_t err = ESP_FAIL;
    
    cJSON *data_json = cJSON_CreateObject();
    cJSON_AddStringToObject(data_json, "token", token);

#ifdef ESP_QCLOUD_IOTHUB_BIND_RETRY

    for (size_t i = 0; i < 3; i++) {
        err = esp_qcloud_iothub_publish("service", "app_bind_token", NULL, data_json);
        cJSON_Delete(data_json);
        ESP_QCLOUD_ERROR_BREAK(err != ESP_OK, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));

        if (xEventGroupWaitBits(g_iothub_group, IOTHUB_EVENT_BOND_RELAY, false, true, pdMS_TO_TICKS(3 * 1000))) {
            break;
        }
    }

#else
    err = esp_qcloud_iothub_publish("service", "app_bind_token", NULL, data_json);
    cJSON_Delete(data_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));
#endif

    if(true == block) {
        EventBits_t bits = xEventGroupWaitBits(g_iothub_group, IOTHUB_EVENT_BIND_SUCCESS | IOTHUB_EVENT_BIND_FAIL, 
                                                                false, false, pdMS_TO_TICKS(QCLOUD_IOTHUB_BINDING_TIMEOUT));
        if (bits & IOTHUB_EVENT_BIND_FAIL) {
            return ESP_FAIL;
        } else if(bits & IOTHUB_EVENT_BIND_SUCCESS) {
            return ESP_OK;
        } else {
            esp_event_post(QCLOUD_EVENT, QCLOUD_EVENT_IOTHUB_BIND_EXCEPTION, NULL, 0, portMAX_DELAY);
            return ESP_ERR_TIMEOUT;
        }
    }

    return err;
}

esp_err_t esp_qcloud_iothub_get_status(esp_qcloud_method_type_t type, bool auto_update)
{
    ESP_QCLOUD_ERROR_CHECK(type != QCLOUD_METHOD_TYPE_REPORT, ESP_ERR_NOT_SUPPORTED, "not support");
    esp_err_t err;
    g_get_status_need_update = auto_update;
    cJSON *data_json = cJSON_CreateObject();
    cJSON_AddStringToObject(data_json, "type", "report");
    err = esp_qcloud_iothub_publish("property", "get_status", NULL, data_json);
    cJSON_Delete(data_json);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));

    return err;
}

esp_err_t esp_qcloud_iothub_report_device_info(void)
{
    esp_err_t err    = ESP_FAIL;
    uint8_t mac[6]   = { 0 };
    char mac_str[31] = { 0 };
    err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "<%s> esp_wifi_get_mac", esp_err_to_name(err));

    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON *data_json = cJSON_CreateObject();
    cJSON *device_label = cJSON_CreateObject();
    cJSON_AddStringToObject(data_json, "module_hardinfo", CONFIG_IDF_TARGET);
    cJSON_AddStringToObject(data_json, "module_softinfo", esp_get_idf_version());
    cJSON_AddStringToObject(data_json, "fw_ver", esp_qcloud_get_version());
    cJSON_AddStringToObject(data_json, "mac", (const char*)&mac_str);

    /*You can add custom information, which will be displayed in the expanded information section of the QCloud*/
    cJSON_AddStringToObject(device_label, "manufacturer", "ESPRESSIF");
    /*No need to delete when using <cJSON_AddItemToObject>*/
    cJSON_AddItemToObject(data_json, "device_label", device_label);

    err = esp_qcloud_iothub_publish("property", "report_info", NULL, data_json);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "<%s> esp_qcloud_iothub_publish", esp_err_to_name(err));

EXIT:
    cJSON_Delete(data_json);
    return err;
}

static void esp_qcloud_iothub_event_callback(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    ESP_LOGI(TAG, "event_callback: topic: %s, payload: %.*s", topic, payload_len, (char *)payload);

    cJSON *root_json  = cJSON_Parse(payload);
    ESP_QCLOUD_ERROR_GOTO(!root_json, EXIT, "The data format is wrong and cannot be parsed");

    const char *method = cJSON_GetObjectItem(root_json, "method")->valuestring;
    ESP_QCLOUD_ERROR_GOTO(!method, EXIT, "The data format is wrong, the 'clientToken' field is not included");

    if (!strcmp(method, "event_reply")) {
        const int result_code = cJSON_GetObjectItem(root_json, "code")->valueint;
        if(0 == result_code){
            ESP_LOGI(TAG, "post event succeed");
        } else {
            ESP_LOGE(TAG, "post event failed, reason code: %d", result_code);
        }
    } 

EXIT:
    cJSON_Delete(root_json);
}

static esp_err_t esp_qcloud_iothub_register_event()
{
    esp_err_t err = ESP_FAIL;

    err = esp_qcloud_iothub_subscribe("event", esp_qcloud_iothub_event_callback);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_subscribe");

    return err;
}

static void esp_qcloud_iothub_action_callback(const char *topic, void *payload, size_t payload_len, void *priv_data)
{
    ESP_LOGI(TAG, "action_callback: topic: %s, payload: %.*s", topic, payload_len, (char *)payload);

    cJSON *root_json  = cJSON_Parse(payload);
    ESP_QCLOUD_ERROR_GOTO(!root_json, EXIT, "The data format is wrong and cannot be parsed");

    const char *method = cJSON_GetObjectItem(root_json, "method")->valuestring;
    ESP_QCLOUD_ERROR_GOTO(!method, EXIT, "The data format is wrong, the 'method' field is not included");

    const char *action_id = cJSON_GetObjectItem(root_json, "actionId")->valuestring;
    ESP_QCLOUD_ERROR_GOTO(!action_id, EXIT, "The data format is wrong, the 'action_id' field is not included");

    char *token = cJSON_GetObjectItem(root_json, "clientToken")->valuestring;
    ESP_QCLOUD_ERROR_GOTO(!token, EXIT, "The data format is wrong, the 'clientToken' field is not included");

    cJSON *params = cJSON_GetObjectItem(root_json, "params");
    ESP_QCLOUD_ERROR_GOTO(!params, EXIT, "The data format is wrong, the 'params' field is not included");
    char *params_str = cJSON_PrintUnformatted(params);

    esp_qcloud_method_t *action = esp_qcloud_iothub_create_action();
    action->extra_val->token    = token;
    action->extra_val->code     = esp_qcloud_operate_action(action, action_id, params_str);

    ESP_QCLOUD_FREE(params_str);
    esp_qcloud_iothub_post_method(action);
    esp_qcloud_iothub_destroy_action(action);

EXIT:
    cJSON_Delete(root_json);
}

static esp_err_t esp_qcloud_iothub_register_action()
{
    esp_err_t err = ESP_FAIL;

    err = esp_qcloud_iothub_subscribe("action", esp_qcloud_iothub_action_callback);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_subscribe");

    return err;
}

esp_err_t esp_qcloud_iothub_start()
{
    esp_err_t err = ESP_FAIL;
    char token[AUTH_TOKEN_MAX_SIZE + 1] = {0};

    err = esp_qcloud_iothub_register_service();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_register_service");

    if (esp_qcloud_storage_get("token", token, AUTH_TOKEN_MAX_SIZE) == ESP_OK) {
        esp_qcloud_iothub_bind(token, true);
        esp_qcloud_storage_erase("token");
    }

    err = esp_qcloud_iothub_register_log();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_report_property");

    err = esp_qcloud_iothub_register_property();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_register_property");

    err = esp_qcloud_iothub_register_event();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_register_event");

    err = esp_qcloud_iothub_register_action();
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_qcloud_iothub_register_action");

    err = esp_qcloud_iothub_report_all_property();
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

esp_err_t esp_qcloud_iothub_param_add_int(esp_qcloud_method_t *method, char *id, int value)
{
    ESP_QCLOUD_ERROR_CHECK(!method || !id, ESP_FAIL, "method or id is a null pointer");

    esp_qcloud_param_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));
    item->id = id;
    item->value.i = (int)value;
    item->value.type = QCLOUD_VAL_TYPE_INTEGER;

    esp_qcloud_param_t *last = SLIST_FIRST(&method->method_param_list);
    if (last == NULL) {
        SLIST_INSERT_HEAD(&method->method_param_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_param_add_float(esp_qcloud_method_t *method, char *id, float value)
{
    ESP_QCLOUD_ERROR_CHECK(!method || !id, ESP_FAIL, "method or id is a null pointer");

    esp_qcloud_param_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));
    item->id = id;
    item->value.f = (float)value;
    item->value.type = QCLOUD_VAL_TYPE_FLOAT;
    
    esp_qcloud_param_t *last = SLIST_FIRST(&method->method_param_list);
    if (last == NULL) {
        SLIST_INSERT_HEAD(&method->method_param_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_param_add_string(esp_qcloud_method_t *method, char *id, char *value)
{
    ESP_QCLOUD_ERROR_CHECK(!method || !id, ESP_FAIL, "method or id is a null pointer");

    esp_qcloud_param_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));
    item->id = id;
    item->value.s = strdup(value);
    item->value.type = QCLOUD_VAL_TYPE_STRING;

    esp_qcloud_param_t *last = SLIST_FIRST(&method->method_param_list);
    if (last == NULL) {
        SLIST_INSERT_HEAD(&method->method_param_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_param_add_bool(esp_qcloud_method_t *method, char* id, bool value)
{
    ESP_QCLOUD_ERROR_CHECK(!method || !id, ESP_FAIL, "method or id is a null pointer");

    esp_qcloud_param_t *item = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_param_t));
    item->id = id;
    item->value.b = value;
    item->value.type = QCLOUD_VAL_TYPE_BOOLEAN;

    esp_qcloud_param_t *last = SLIST_FIRST(&method->method_param_list);
    if (last == NULL) {
        SLIST_INSERT_HEAD(&method->method_param_list, item, next);
    } else {
        SLIST_INSERT_AFTER(last, item, next);
    }

    return ESP_OK;
}

esp_qcloud_method_t *esp_qcloud_iothub_create_report(void)
{
    esp_qcloud_method_t *report  = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_method_t));
    report->method_type          = QCLOUD_METHOD_TYPE_REPORT;
    report->extra_val            = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_method_extra_val_t));
    report->extra_val->timestamp = esp_log_timestamp();

    return report;
}

esp_qcloud_method_t *esp_qcloud_iothub_create_event(const char *eventId, esp_qcloud_event_type_t type)
{
    static char *event_type_str_list[6] = {"info", "alert", "fault"};

    esp_qcloud_method_t *event  = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_method_t));
    event->method_type          = QCLOUD_METHOD_TYPE_EVENT;
    event->extra_val            = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_method_extra_val_t));
    event->extra_val->id        = strdup(eventId);
    event->extra_val->version   = EVENT_VERSION;
    event->extra_val->type      = event_type_str_list[type - 1];

    return event;
}

esp_qcloud_method_t *esp_qcloud_iothub_create_action(void)
{
    esp_qcloud_method_t *action = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_method_t));
    action->method_type         = QCLOUD_METHOD_TYPE_ACTION_REPLY;
    action->extra_val           = ESP_QCLOUD_CALLOC(1, sizeof(esp_qcloud_method_extra_val_t));

    return action;
}

esp_err_t esp_qcloud_iothub_destroy_report(esp_qcloud_method_t *report)
{
    ESP_QCLOUD_ERROR_CHECK(!report, ESP_FAIL, "report is a null pointer");

    while (!SLIST_EMPTY(&report->method_param_list)) {
        esp_qcloud_param_t *item = SLIST_FIRST(&report->method_param_list);	
	    SLIST_REMOVE_HEAD(&report->method_param_list, next);
        if(item->value.type == QCLOUD_VAL_TYPE_STRING){
            ESP_QCLOUD_FREE(item->value.s);
        }
        ESP_QCLOUD_FREE(item);
    }
    ESP_QCLOUD_FREE(report->extra_val);
    ESP_QCLOUD_FREE(report);

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_destroy_event(esp_qcloud_method_t *event)
{
    ESP_QCLOUD_ERROR_CHECK(!event, ESP_FAIL, "event is a null pointer");

    while (!SLIST_EMPTY(&event->method_param_list)) {
        esp_qcloud_param_t *item = SLIST_FIRST(&event->method_param_list);	
	    SLIST_REMOVE_HEAD(&event->method_param_list, next);
        if(item->value.type == QCLOUD_VAL_TYPE_STRING){
            ESP_QCLOUD_FREE(item->value.s);
        }
        ESP_QCLOUD_FREE(item);
    }
    ESP_QCLOUD_FREE(event->extra_val->id);
    ESP_QCLOUD_FREE(event->extra_val);
    ESP_QCLOUD_FREE(event);

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_destroy_action(esp_qcloud_method_t *action)
{
    ESP_QCLOUD_ERROR_CHECK(!action, ESP_FAIL, "action is a null pointer");

    while (!SLIST_EMPTY(&action->method_param_list)) {
        esp_qcloud_param_t *item = SLIST_FIRST(&action->method_param_list);	
	    SLIST_REMOVE_HEAD(&action->method_param_list, next);
        if(item->value.type == QCLOUD_VAL_TYPE_STRING){
            ESP_QCLOUD_FREE(item->value.s);
        }
        ESP_QCLOUD_FREE(item);
    }
    ESP_QCLOUD_FREE(action->extra_val);
    ESP_QCLOUD_FREE(action);

    return ESP_OK;
}

static esp_err_t esp_qcloud_get_topic_and_method_name(char **topic_name, char **method_name, esp_qcloud_method_type_t type)
{
    static char *topic_name_list[TOPIC_METHOD_NAME_MAX_SIZE] = {"event", "action", "service", "property"};
    static char *method_name_list[TOPIC_METHOD_NAME_MAX_SIZE] = {"event_post", "action_reply", "", "report"};
    if(type == QCLOUD_METHOD_TYPE_INVALID || type >= QCLOUD_METHOD_TYPE_MAX_INVALID || topic_name == NULL || method_name == NULL){
        return ESP_FAIL;
    } else if (type == QCLOUD_METHOD_TYPE_EVENT){
        *topic_name = topic_name_list[0];
    } else if (type == QCLOUD_METHOD_TYPE_ACTION_REPLY) {
        *topic_name = topic_name_list[1];
    } else if (type == QCLOUD_METHOD_TYPE_APP_BIND_TOKEN) {
        return ESP_ERR_NOT_SUPPORTED;
    } else {
        *topic_name = topic_name_list[3];
    } 
    *method_name = method_name_list[type - 1];

    return ESP_OK;
}

esp_err_t esp_qcloud_iothub_post_method(esp_qcloud_method_t *method)
{
    ESP_QCLOUD_ERROR_CHECK(!method, ESP_FAIL, "method is a null pointer");

    esp_qcloud_param_t *param;
    esp_err_t err      = ESP_FAIL;
    char *topic_name   = NULL;
    char *method_name  = NULL;
    cJSON *params_json = cJSON_CreateObject();
    
    SLIST_FOREACH(param, &method->method_param_list, next) {
        ESP_LOGD(TAG, "esp_qcloud_iothub_post_method %s", param->id);
        if (param->value.type == QCLOUD_VAL_TYPE_INTEGER) {
            cJSON_AddNumberToObject(params_json, param->id, param->value.i);
        } else if (param->value.type == QCLOUD_VAL_TYPE_BOOLEAN) {
            cJSON_AddNumberToObject(params_json, param->id, param->value.b);
        } else if (param->value.type == QCLOUD_VAL_TYPE_STRING) {
            cJSON_AddStringToObject(params_json, param->id, param->value.s);
        } else if (param->value.type == QCLOUD_VAL_TYPE_FLOAT) {
            cJSON_AddNumberToObject(params_json, param->id, param->value.f);
        }
    }

    err = esp_qcloud_get_topic_and_method_name((char **)&topic_name, (char **)&method_name, method->method_type);
    ESP_QCLOUD_ERROR_GOTO(err != ESP_OK, EXIT, "get topic or method name fail");

    err = esp_qcloud_iothub_publish(topic_name, method_name, method->extra_val, params_json);

EXIT:
    cJSON_Delete(params_json);
    return err;
}
