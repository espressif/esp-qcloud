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

#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "driver/uart.h"

#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#include "esp_qcloud_log.h"

#define PROMPT_STR CONFIG_IDF_TARGET

static bool g_running_flag = false;
static const char *TAG = "esp_qcloud_console";

static void initialize_console(void)
{
    /* Drain stdout before reconfiguring it */
    fflush(stdout);
    fsync(fileno(stdout));

    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

#if ESP_IDF_VERSION > ESP_IDF_VERSION_VAL(4, 2, 0)
    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
#else  
    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
#endif

    /* Configure UART. Note that REF_TICK is used so that the baud rate remains
     * correct while APB frequency is changing in light sleep mode.
     */
    const uart_config_t uart_config = {
        .baud_rate  = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        
#ifdef CONFIG_IDF_TARGET_ESP32C3
        .source_clk = UART_SCLK_XTAL,
#else
        .source_clk = UART_SCLK_REF_TICK,
#endif

    };
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_QCLOUD_CONSOLE_UART_NUM,
                                        256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(CONFIG_QCLOUD_CONSOLE_UART_NUM, &uart_config));

    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(CONFIG_QCLOUD_CONSOLE_UART_NUM);

    /* Initialize the console */
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
#if CONFIG_LOG_COLORS
        .hint_color = atoi(LOG_COLOR_CYAN)
#endif
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    /* Configure linenoise line completion library */
    /* Enable multiline editing. If not set, long commands will scroll within
     * single line.
     */
    linenoiseSetMultiLine(1);

    /* Tell linenoise where to get command completions and hints */
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback *) &esp_console_get_hint);

    /* Set command history size */
    linenoiseHistorySetMaxLen(100);

    /* Don't return empty lines */
    linenoiseAllowEmpty(false);
}

static void console_handle_task(void *arg)
{
    const char *prompt = LOG_COLOR_I PROMPT_STR "> " LOG_RESET_COLOR;

#if CONFIG_LOG_COLORS
    /* Since the terminal doesn't support escape sequences,
     * don't use color codes in the prompt.
     */
    prompt = PROMPT_STR "> ";
#endif //CONFIG_LOG_COLORS

    while (g_running_flag) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         */
        char *line = linenoise(prompt);

        if (line == NULL) { /* Break on EOF or error */
            continue;
        }

        /* Add the command to the history if not empty*/
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
        }

        /* Try to run the command */
        int ret;
        esp_err_t err = esp_console_run(line, &ret);

        if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "Unrecognized command");
        } else if (err == ESP_ERR_INVALID_ARG) {
            // command was empty
        } else if (err == ESP_OK && ret != ESP_OK) {
            ESP_LOGW(TAG, "Command returned non-zero error code: 0x%x (%s)", ret, esp_err_to_name(ret));
        } else if (err != ESP_OK) {
            ESP_LOGW(TAG, "Internal error: %s", esp_err_to_name(err));
        }

        /* linenoise allocates line buffer on the heap, so need to free it */
        linenoiseFree(line);
    }

    ESP_LOGE(TAG, "Error or end-of-input, terminating console");
    vTaskDelete(NULL);
}

esp_err_t esp_qcloud_console_init()
{
    initialize_console();

    /* Register commands */
    esp_console_register_help_command();
    extern void esp_qcloud_commands_register_common();
    esp_qcloud_commands_register_common();

    /* Prompt to be printed before each line.
     * This can be customized, made dynamic, etc.
     */
    printf("\n"
           "This is an example of ESP-IDF console component.\n"
           "Type 'help' to get the list of commands.\n"
           "Use UP/DOWN arrows to navigate through command history.\n"
           "Press TAB when typing command name to auto-complete.\n"
           "Press Enter or Ctrl+C will terminate the console environment.\n");

    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();

    if (probe_status) { /* zero indicates success */
        printf("\n"
               "Your terminal application does not support escape sequences.\n"
               "Line editing and history features are disabled.\n"
               "On Windows, try using Putty instead.\n");
        linenoiseSetDumbMode(1);
    }

    g_running_flag = true;
    xTaskCreate(console_handle_task, "console_handle", 1024 * 4, NULL, 1, NULL);

    return ESP_OK;
}

esp_err_t esp_qcloud_console_deinit()
{
    esp_err_t ret = ESP_OK;
    g_running_flag = false;

    ret = esp_console_deinit();
    ESP_QCLOUD_ERROR_CHECK(ret != ESP_OK, ret, "de-initialize console module");

    return ESP_OK;
}
