idf_component_register(SRCS "print_queue.c"
                       INCLUDE_DIRS "include"
                       REQUIRES common_util
                       PRIV_REQUIRES usb_printer brother_protocol heap esp_timer)
