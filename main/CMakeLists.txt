idf_component_register(SRCS "main.cpp" 
                    REQUIRES spi_flash esp_wifi spiffs  mdns nvs_flash esp_http_server app_update EspMeshCompact
                    INCLUDE_DIRS "")
