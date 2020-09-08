/* LED Light Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_qcloud_log.h"
#include "esp_qcloud_console.h"
#include "esp_qcloud_storage.h"
#include <esp_qcloud_iothub.h>
#include <esp_qcloud_prov.h>

#include "app_priv.h"

static const char *TAG = "app_main";

/* Callback to handle commands received from the QCloud cloud */
static esp_err_t light_get_param(const char *id, esp_qcloud_param_val_t *val)
{
    if (!strcmp(id, "power_switch")) {
        val->b = app_light_get_power();
    } else if (!strcmp(id, "value")) {
        val->i = app_light_get_value();
    } else if (!strcmp(id, "hue")) {
        val->i = app_light_get_hue();
    } else if (!strcmp(id, "saturation")) {
        val->i = app_light_get_saturation();
    }

    ESP_LOGI(TAG, "Report id: %s, val: %d", id, val->i);

    return ESP_OK;
}

/* Callback to handle commands received from the QCloud cloud */
static esp_err_t light_set_param(const char *id, const esp_qcloud_param_val_t *val)
{
    esp_err_t err = ESP_FAIL;
    ESP_LOGI(TAG, "Received id: %s, val: %d", id, val->i);

    if (!strcmp(id, "power_switch")) {
        err = app_light_set_power(val->b);
    } else if (!strcmp(id, "value")) {
        err = app_light_set_value(val->i);
    } else if (!strcmp(id, "hue")) {
        err = app_light_set_hue(val->i);
    } else if (!strcmp(id, "saturation")) {
        err = app_light_set_saturation(val->i);
    } else {
        ESP_LOGW(TAG, "This parameter is not supported");
    }

    return err;
}

/* Event handler for catching QCloud events */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int event_id, void *event_data)
{
    switch (event_id) {
        case QCLOUD_EVENT_INIT_DONE:
            ESP_LOGI(TAG, "QCloud Initialised.");
            break;

        default:
            ESP_LOGW(TAG, "Unhandled QCloud Event: %d", event_id);
    }
}

void app_main()
{
    ESP_ERROR_CHECK(esp_qcloud_storage_init());

    /**
     * @brief Add debug function, you can use serial command and remote debugging.
     */
    esp_qcloud_log_config_t log_config = {
        .log_level_uart = ESP_LOG_INFO,
    };
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_ERROR_CHECK(esp_qcloud_log_init(&log_config));
    ESP_ERROR_CHECK(esp_qcloud_console_init());

    /**
     * @brief Initialize Application specific hardware drivers and set initial state.
     */
    app_driver_init();
    app_light_get_power(DEFAULT_POWER);
    app_light_get_hue(DEFAULT_HUE);
    app_light_get_saturation(DEFAULT_SATURATION);
    app_light_set_value(DEFAULT_VALUE);

    /* Initialize Wi-Fi. Note that, this should be called before esp_qcloud_wifi_start()
     */
    ESP_ERROR_CHECK(esp_qcloud_wifi_init());
    ESP_ERROR_CHECK(esp_event_handler_register(QCLOUD_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));

    /*
     * @breif Create a device through the server and obtain configuration parameters
     *        server: https://console.cloud.tencent.com/iotexplorer
     */
#ifdef CONFIG_QCLOUD_MASS_PRODUCTION
    /**
     * @brief Read device configuration information through flash
     *        1. Configure device information via device_info.csv
     *        2. Generate device_info.bin, use the following command:
     *          python $IDF_PATH/components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py generate device_info.csv device_info.bin 0x8000 --version 2
     *        3. Burn device_info.bin to flash
     *          python $IDF_PATH/components/esptool_py/esptool/esptool.py write_flash 0x15000 device_info.bin
     */
    ESP_ERROR_CHECK(esp_qcloud_create_device(esp_qcloud_storage_get("product_id"),
                    esp_qcloud_storage_get("device_name")));
    ESP_ERROR_CHECK(esp_qcloud_device_secret(esp_qcloud_storage_get("device_secret")));
#else
    /**
     * @brief Read device configuration information through sdkconfig.h
     *        1. Configure device information via `idf.py menuconfig`, Menu path: (Top) -> Example Configuration
     *
     */
    ESP_ERROR_CHECK(esp_qcloud_create_device(CONFIG_QCLOUD_PRODUCT_ID, CONFIG_QCLOUD_DEVICE_NAME));
    ESP_ERROR_CHECK(esp_qcloud_device_secret(CONFIG_QCLOUD_DEVICE_SECRET));
#endif

    /**< Configure the version of the device, and use this information to determine whether to OTA */
    ESP_ERROR_CHECK(esp_qcloud_device_version("0.0.1"));
    /**< Register the properties of the device */
    ESP_ERROR_CHECK(esp_qcloud_device_param("power_switch", esp_qcloud_bool(DEFAULT_POWER)));
    ESP_ERROR_CHECK(esp_qcloud_device_param("hue", esp_qcloud_int(DEFAULT_HUE)));
    ESP_ERROR_CHECK(esp_qcloud_device_param("saturation", esp_qcloud_int(DEFAULT_SATURATION)));
    ESP_ERROR_CHECK(esp_qcloud_device_param("value", esp_qcloud_int(DEFAULT_VALUE)));
    /**< The processing function of the communication between the device and the server */
    ESP_ERROR_CHECK(esp_qcloud_device_handle(light_get_param, light_set_param));

    /**
     * @brief Configure router and cloud binding information
     */
    char *token             = esp_qcloud_storage_get("token");
    wifi_config_t *wifi_cfg = esp_qcloud_storage_get("wifi_config");

    if (!token || !wifi_cfg) {
        esp_qcloud_prov_softap_start("tcloud_XXX", NULL, NULL);
        ESP_ERROR_CHECK(esp_qcloud_prov_wait(&wifi_cfg, &token, portMAX_DELAY));
        esp_qcloud_prov_softap_stop();

        esp_qcloud_storage_set("token", token, strlen(token) + 1);
        esp_qcloud_storage_set("wifi_config", wifi_cfg, sizeof(wifi_config_t));
    }

    /**
     * @brief Connect to router
     */
    ESP_ERROR_CHECK(esp_qcloud_wifi_start(wifi_cfg));
    ESP_ERROR_CHECK(esp_qcloud_timesync_start());

    /**
     * @brief Connect to Tencent Cloud Iothub
     */
    ESP_ERROR_CHECK(esp_qcloud_iothub_init());
    ESP_ERROR_CHECK(esp_qcloud_iothub_bind(token));
    ESP_ERROR_CHECK(esp_qcloud_iothub_ota_enable());
    ESP_ERROR_CHECK(esp_qcloud_iothub_start());

    ESP_QCLOUD_FREE(token);
    ESP_QCLOUD_FREE(wifi_cfg);
}
