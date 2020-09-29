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

#include <sys/param.h>
#include "argtable3/argtable3.h"
#include "mbedtls/base64.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_console.h"

#include "esp_qcloud_log.h"
#include "esp_qcloud_console.h"

#define CONFIG_QCLOUD_LOG_MAX_SIZE 1024

static const char *TAG = "esp_qcloud_commands";

static struct {
    struct arg_lit *length;
    struct arg_lit *output;
    struct arg_lit *erase;
    struct arg_end *end;
} coredump_args;

static struct {
    struct arg_str *tag;
    struct arg_str *level;
    struct arg_str *mode;
    struct arg_lit *status;
    struct arg_lit *read;
    struct arg_end *end;
} log_args;

/**
 * @brief  A function which implements version command.
 */
static int version_func(int argc, char **argv)
{
    esp_chip_info_t chip_info = {0};

    /**< Pint system information */
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "compile time     : %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "free heap        : %d Bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "CPU cores        : %d", chip_info.cores);
    ESP_LOGI(TAG, "silicon revision : %d", chip_info.revision);
    ESP_LOGI(TAG, "feature          : %s%s%s%s%d%s",
             chip_info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
             chip_info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
             chip_info.features & CHIP_FEATURE_BT ? "/BT" : "",
             chip_info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
             spi_flash_get_chip_size() / (1024 * 1024), " MB");

    return ESP_OK;
}

/**
 * @brief  Register version command.
 */
static void register_version()
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &version_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements log command.
 */
static int log_func(int argc, char **argv)
{
    const char *level_str[6] = {"NONE", "ERR", "WARN", "INFO", "DEBUG", "VER"};
    esp_qcloud_log_config_t log_config = {0};

    if (arg_parse(argc, argv, (void **)&log_args) != ESP_OK) {
        arg_print_errors(stderr, log_args.end, argv[0]);
        return ESP_FAIL;
    }

    esp_qcloud_log_get_config(&log_config);

    for (int log_level = 0; log_args.level->count && log_level < sizeof(level_str) / sizeof(char *); ++log_level) {
        if (!strcasecmp(level_str[log_level], log_args.level->sval[0])) {
            const char *tag = log_args.tag->count ? log_args.tag->sval[0] : "*";

            if (!log_args.mode->count) {
                esp_log_level_set(tag, log_level);
            } else {
                if (!strcasecmp(log_args.mode->sval[0], "flash")) {
                    log_config.log_level_flash = log_level;
                } else if (!strcasecmp(log_args.mode->sval[0], "uart")) {
                    log_config.log_level_uart = log_level;
                } else if (!strcasecmp(log_args.mode->sval[0], "local")) {
                    log_config.log_level_local = log_level;
                } else if (!strcasecmp(log_args.mode->sval[0], "iothub")) {
                    log_config.log_level_iothub = log_level;
                }

                esp_qcloud_log_set_config(&log_config);
            }
        }
    }

    if (log_args.status->count) { /**< Output enable type */
        ESP_LOGI(TAG, "uart log level: %s", level_str[log_config.log_level_uart]);
        ESP_LOGI(TAG, "flash log level: %s", level_str[log_config.log_level_flash]);
        ESP_LOGI(TAG, "local log level: %s", level_str[log_config.log_level_local]);
        ESP_LOGI(TAG, "iothub log level: %s", level_str[log_config.log_level_iothub]);
    }

    if (log_args.read->count) {  /**< read to the flash of log data */
        int log_size   = esp_qcloud_log_flash_size();
        char *log_data = ESP_QCLOUD_MALLOC(CONFIG_QCLOUD_LOG_MAX_SIZE - 17);

        ESP_LOGI(TAG, "The flash partition that stores the log size: %d", log_size);

        for (size_t size = MIN(CONFIG_QCLOUD_LOG_MAX_SIZE - 17, log_size);
                size > 0 && esp_qcloud_log_flash_read(log_data, &size) == ESP_OK;
                log_size -= size, size = MIN(CONFIG_QCLOUD_LOG_MAX_SIZE - 17, log_size)) {
            printf("%.*s", size, log_data);
            fflush(stdout);
        }

        ESP_QCLOUD_FREE(log_data);
    }

    return ESP_OK;
}

/**
 * @brief  Register log command.
 */
static void register_log()
{
    log_args.tag    = arg_str0("t", "tag", "<tag>", "Tag of the log entries to enable, '*' resets log level for all tags to the given value");
    log_args.level  = arg_str0("l", "level", "<level>", "Selects log level to enable (NONE, ERR, WARN, INFO, DEBUG, VER)");
    log_args.mode   = arg_str0("m", "mode", "<mode('uart', 'flash', 'local' or 'iothub')>", "Selects log to mode ('uart', 'flash', 'local' or 'iothub')");
    log_args.status = arg_lit0("s", "status", "Configuration of output log");
    log_args.read   = arg_lit0("r", "read", "Read to the flash of log information");
    log_args.end    = arg_end(8);

    const esp_console_cmd_t cmd = {
        .command = "log",
        .help = "Set log level for given tag",
        .hint = NULL,
        .func = &log_func,
        .argtable = &log_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements restart command.
 */
static int restart_func(int argc, char **argv)
{
    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

/**
 * @brief  Register restart command.
 */
static void register_restart()
{
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help    = "Software reset of the chip",
        .hint    = NULL,
        .func    = &restart_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements reset command.
 */
static int reset_func(int argc, char **argv)
{
    esp_err_t ret = ESP_OK;
    esp_partition_iterator_t part_itra = NULL;
    const esp_partition_t *nvs_part = NULL;

    ESP_LOGI(TAG, "Erase part of the nvs partition");

    part_itra = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "nvs");
    ESP_QCLOUD_ERROR_CHECK(!part_itra, ESP_ERR_NOT_SUPPORTED, "partition no find, subtype: 0x%x, label: %s",
                           ESP_PARTITION_SUBTYPE_ANY, "nvs");

    nvs_part = esp_partition_get(part_itra);
    ESP_QCLOUD_ERROR_CHECK(!nvs_part, ESP_ERR_NOT_SUPPORTED, "esp_partition_get");

    ret = esp_partition_erase_range(nvs_part, 0, nvs_part->size);
    ESP_QCLOUD_ERROR_CHECK(ret != ESP_OK, ret, "Erase part of the nvs partition");

    ESP_LOGI(TAG, "Restarting");
    esp_restart();
}

/**
 * @brief  Register reset command.
 */
static void register_reset()
{
    const esp_console_cmd_t cmd = {
        .command = "reset",
        .help = "Clear device configuration information",
        .hint = NULL,
        .func = &reset_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements reset command.
 */
static int fallback_func(int argc, char **argv)
{
    esp_err_t err = ESP_OK;
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);

    err = esp_ota_set_boot_partition(partition);
    ESP_QCLOUD_ERROR_CHECK(err != ESP_OK, err, "esp_ota_set_boot_partition failed!");

    ESP_LOGI(TAG, "The next reboot will fall back to the previous version");

    return ESP_OK;
}

/**
 * @brief  Register fallback command.
 */
static void register_fallback()
{
    const esp_console_cmd_t cmd = {
        .command = "fallback",
        .help = "Upgrade error back to previous version",
        .hint = NULL,
        .func = &fallback_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements heap command.
 */
static int heap_func(int argc, char **argv)
{
    esp_qcloud_mem_print_record();
    esp_qcloud_mem_print_heap();
    esp_qcloud_mem_print_task();

    if (!heap_caps_check_integrity_all(true)) {
        ESP_LOGE(TAG, "At least one heap is corrupt");
    }

    return ESP_OK;
}

/**
 * @brief  Register heap command.
 */
static void register_heap()
{
    const esp_console_cmd_t cmd = {
        .command = "heap",
        .help = "Get the current size of free heap memory",
        .hint = NULL,
        .func = &heap_func,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

/**
 * @brief  A function which implements coredump command.
 */
static int coredump_func(int argc, char **argv)
{
    esp_err_t ret        = ESP_OK;
    ssize_t coredump_size = 0;
    const esp_partition_t *coredump_part = NULL;

    if (arg_parse(argc, argv, (void **)&coredump_args) != ESP_OK) {
        arg_print_errors(stderr, coredump_args.end, argv[0]);
        return ESP_FAIL;
    }

    coredump_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                    ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
    ESP_QCLOUD_ERROR_CHECK(coredump_part == NULL, ESP_ERR_NOT_SUPPORTED, "No core dump partition found!");

    ret = esp_partition_read(coredump_part, 4, &coredump_size, sizeof(size_t));
    ESP_QCLOUD_ERROR_CHECK(coredump_part == NULL, ESP_ERR_NOT_SUPPORTED, "Core dump read length!");

    if (coredump_args.length->count) {
        ESP_LOGI(TAG, "Core dump is length: %d Bytes", coredump_size);
    }

    if (coredump_args.output->count && coredump_size > 0) {
#define COREDUMP_BUFFER_SIZE 1024
        uint8_t *buffer = ESP_QCLOUD_REALLOC_RETRY(NULL, COREDUMP_BUFFER_SIZE);
        ESP_LOGI(TAG, "\n================= CORE DUMP START =================\n");

        for (int offset = 4; offset < coredump_size; offset += COREDUMP_BUFFER_SIZE) {
            size_t size = MIN(COREDUMP_BUFFER_SIZE, coredump_size - offset);
            esp_partition_read(coredump_part, offset, buffer, size);
            size_t dlen = (size + 2) / 3 * 4; //base64 encode maximum length = ⌈ n / 3 ⌉ * 4
            size_t olen = 0;
            uint8_t *b64_buf = ESP_QCLOUD_MALLOC(dlen);
            mbedtls_base64_encode(b64_buf, dlen, &olen, buffer, size);
            printf("%s", b64_buf);
            ESP_QCLOUD_FREE(b64_buf);
        }

        ESP_LOGI(TAG, "================= CORE DUMP END ===================\n");

        ESP_LOGI(TAG, "1. Save core dump text body to some file manually");
        ESP_LOGI(TAG, "2. Run the following command: \n"
                 "python $ESP_QCLOUD_PATH/esp-idf/components/espcoredump/espcoredump.py info_corefile -t b64 -c </path/to/saved/base64/text> </path/to/program/elf/file>");
        ESP_QCLOUD_FREE(buffer);
    }

    if (coredump_args.erase->count) {
        ret = esp_partition_erase_range(coredump_part, 0, coredump_part->size);
        ESP_QCLOUD_ERROR_CHECK(ret != ESP_OK, ESP_FAIL, "Core dump erase fail");
        ESP_LOGI(TAG, "Core dump erase successful");
    }

    return ESP_OK;
}

/**
 * @brief  Register coredump command.
 */
static void register_coredump()
{
    coredump_args.length = arg_lit0("l", "length", "Get coredump data length");
    coredump_args.output = arg_lit0("o", "output", "Read the coredump data of the device");
    coredump_args.erase  = arg_lit0("e", "erase", "Erase the coredump data of the device");
    coredump_args.end    = arg_end(5);

    const esp_console_cmd_t cmd = {
        .command = "coredump",
        .help = "Get core dump information",
        .hint = NULL,
        .func = &coredump_func,
        .argtable = &coredump_args,
    };

    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

void esp_qcloud_commands_register_common()
{
    register_version();
    register_heap();
    register_restart();
    register_reset();
    register_fallback();
    register_log();
    register_coredump();
}
