idf_component_register(SRCS "app_main.cpp" "camera.cpp" "device_web.cpp" "diagnostic_replay.cpp"
    INCLUDE_DIRS "."
    REQUIRES people_counter esp_video esp_cam_sensor_imx pedestrian_detect
             nvs_flash esp_timer esp_driver_uart esp_driver_jpeg
             esp_wifi esp_wifi_remote esp_hosted esp_netif esp_event
             esp_http_server mdns json mbedtls captive_dns lwip
    EMBED_TXTFILES "../tools/monitor.html")
if(NOT EXISTS "${PROJECT_DIR}/config/imx219_default.json")
    message(FATAL_ERROR "Missing IMX219 ISP tuning: auto-exposure cannot work.")
endif()
