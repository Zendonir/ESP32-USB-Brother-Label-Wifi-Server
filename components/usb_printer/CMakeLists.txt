idf_component_register(SRCS "usb_printer.c" "usb_desc_report.c"
                       INCLUDE_DIRS "include"
                       REQUIRES usb common_util
                       PRIV_REQUIRES heap)
