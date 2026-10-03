idf_component_register(
    SRCS "main.c"
         "jianlu_app.c"
         "jianlu_store.c"
         "jianlu_json.c"
         "jianlu_hub.c"
         "jianlu_ui.c"
         "jianlu_voice.c"
         "jianlu_capture.c"
         "../assets/fonts/jianlu_font_16.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_http_client esp_netif esp_timer esp_wifi json nvs_flash spiffs
)
