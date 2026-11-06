idf_component_register(
    SRCS "app_main.c" "bus_uart.c" "ui_seg.c"
         "../../core/panel.c" "../../core/mb_master.c" "../../core/drive_update.c" "../../core/json_api.c" "../../core/seg7.c"
    INCLUDE_DIRS "." "../../core" "../../../comm"
    REQUIRES esp_wifi esp_http_server esp_https_ota esp_http_client mqtt mdns nvs_flash
             wifi_provisioning esp_timer driver app_update)
