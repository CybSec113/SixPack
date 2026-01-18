set(SRCS "config.h")

if(CONFIG_INSTRUMENT_AIRSPEED)
    list(APPEND SRCS "airspeed.c")
elseif(CONFIG_INSTRUMENT_GYRO_COMPASS)
    list(APPEND SRCS "gyrocompass.c")
elseif(CONFIG_INSTRUMENT_INPUTS)
    list(APPEND SRCS "inputs.c")
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash esp_netif lwip driver esp_http_client)
