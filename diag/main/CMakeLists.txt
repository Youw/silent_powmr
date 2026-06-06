# Compile the diagnostic app together with the portable, host-tested core/.
file(GLOB CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/../../core/src/*.cpp)

idf_component_register(
  SRCS
    "diag_main.cpp"
    "wifi_prov.cpp"
    "diag_web.cpp"
    "diag_probe.cpp"
    "uart_transport.cpp"
    ${CORE_SRCS}
  INCLUDE_DIRS
    "."
    "${CMAKE_CURRENT_LIST_DIR}/../../core/include"
  REQUIRES
    esp_wifi esp_event esp_netif nvs_flash esp_http_server driver esp_timer
    app_update esp_app_format)
