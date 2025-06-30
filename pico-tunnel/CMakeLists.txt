cmake_minimum_required(VERSION 3.13)

# initialize pico-sdk from GIT
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_FETCH_FROM_GIT on)

# pico_sdk_import.cmake is a single file copied from this SDK
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(pico_poc)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()


# Add an option to set sync/async mode for UART
option(UART_ASYNCHRONOUS "Enable UART async mode" OFF)


if (UART_ASYNCHRONOUS)
    set(CMAKE_C_FLAGS_UART "-DUART_ASYNCHRONOUS")
endif()

# Set up a C preprocessor definition based on the LOG_LEVEL value
if(PICO_LOG_LEVEL STREQUAL "3")
    add_definitions(-DPICO_LOG_LEVEL=3)
elseif(PICO_LOG_LEVEL STREQUAL "1")
    add_definitions(-DPICO_LOG_LEVEL=1)
else()
    add_definitions(-DPICO_LOG_LEVEL=2)
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_DEBUG} ${CMAKE_C_FLAGS_UART}")
message("-- Target compiler flags are: ${CMAKE_C_FLAGS}")

# rest of your project
if (TARGET tinyusb_device)
    add_executable(pico_poc
        pico_poc.c
    )

    add_library(util
        STATIC
        util/util.h
        util/util.c
    )

    # pull in common dependencies
    target_link_libraries(util pico_stdlib pico_stdio_usb pico_multicore hardware_pwm)
    target_link_libraries(pico_poc PRIVATE util pico_stdlib pico_stdio_usb pico_multicore hardware_pwm)

    # enable usb output, disable uart output
    pico_enable_stdio_usb(pico_poc 1)
    pico_enable_stdio_uart(pico_poc 0)

    # create map/bin/hex/uf2 file etc.
    pico_add_extra_outputs(pico_poc)
elseif(PICO_ON_DEVICE)
    message(WARNING "not building pico_poc because TinyUSB submodule is not initialized in the SDK")
endif()