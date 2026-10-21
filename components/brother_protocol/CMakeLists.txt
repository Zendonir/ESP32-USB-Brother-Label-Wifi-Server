idf_component_register(SRCS "brother_status.c" "brother_printer.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common_util
                       PRIV_REQUIRES usb_printer esp_timer)
