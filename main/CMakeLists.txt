idf_component_register(SRCS "main.c" "console_cmds.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES console esp_driver_gpio app_update esp_timer heap
                                     common_util configuration diagnostics usb_printer
                                     wifi_manager web_interface brother_protocol)
