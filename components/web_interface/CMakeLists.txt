idf_component_register(SRCS "web_server.c"
                       INCLUDE_DIRS "include"
                       EMBED_TXTFILES "${PROJECT_DIR}/web/index.html"
                                      "${PROJECT_DIR}/web/app.js"
                                      "${PROJECT_DIR}/web/style.css"
                       PRIV_REQUIRES esp_http_server esp_app_format app_update bootloader_support esp_timer heap lwip
                                     common_util diagnostics usb_printer wifi_manager brother_protocol print_queue raw_tcp_server)
