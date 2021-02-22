idf_component_register(SRC_DIRS "src/console" "src/iothub" "src/log" "src/mqtt" "src/provisioning" "src/utils" "src/provisioning/qrcode/src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src/provisioning/qrcode/include"
                       REQUIRES "wifi_provisioning" "json" "mqtt" "app_update" "esp_https_ota" "console" "fatfs")
target_compile_options(${COMPONENT_LIB} PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_VERBOSE)

if(CONFIG_AUTH_MODE_CERT)
target_add_binary_data(${COMPONENT_TARGET} "${QCLOUD_PATH}/config/certs/qcloud_root_cert.crt" TEXT)
target_add_binary_data(${COMPONENT_TARGET} "${QCLOUD_PATH}/config/certs/dev_cert.crt" TEXT) 
target_add_binary_data(${COMPONENT_TARGET} "${QCLOUD_PATH}/config/certs/dev_private.key" TEXT)   
endif()
