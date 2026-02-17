set(SRCS "config.h")

if(CONFIG_INSTRUMENT_AIRSPEED)
    list(APPEND SRCS "airspeed.c")
elseif(CONFIG_INSTRUMENT_ATTITUDE)
    list(APPEND SRCS "attitude.c")
elseif(CONFIG_INSTRUMENT_ALTIMETER)
    list(APPEND SRCS "altimeter.c")
elseif(CONFIG_INSTRUMENT_TURN)
    list(APPEND SRCS "turn.c")
elseif(CONFIG_INSTRUMENT_GYRO_COMPASS)
    list(APPEND SRCS "gyrocompass.c")
elseif(CONFIG_INSTRUMENT_INPUTS)
    list(APPEND SRCS "inputs.c")
elseif(CONFIG_INSTRUMENT_VERTSPEED)
    list(APPEND SRCS "vertspeed.c")
endif()

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES esp_wifi nvs_flash esp_netif lwip driver esp_http_client)
