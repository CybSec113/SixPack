# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Define instrument configurations
set(INSTRUMENT_CONFIGS airspeed gyro heading altitude vsi)

# Detect which sdkconfig variant to use from environment or default to airspeed
if(NOT DEFINED INSTRUMENT)
    set(INSTRUMENT "airspeed")
endif()

# Validate instrument choice
if(NOT INSTRUMENT IN_LIST INSTRUMENT_CONFIGS)
    message(FATAL_ERROR "Invalid INSTRUMENT '${INSTRUMENT}'. Must be one of: ${INSTRUMENT_CONFIGS}")
endif()

# Set SDKCONFIG to instrument-specific defaults
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults.esp32c3.${INSTRUMENT}")

if(NOT EXISTS "${SDKCONFIG_DEFAULTS}")
    message(FATAL_ERROR "Config file not found: ${SDKCONFIG_DEFAULTS}")
endif()

message(STATUS "Building for instrument: ${INSTRUMENT}")
message(STATUS "Using config: ${SDKCONFIG_DEFAULTS}")

idf_build_set_property(MINIMAL_BUILD ON)

project(esp-panel-${INSTRUMENT})
