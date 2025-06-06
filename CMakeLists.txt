# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hci_uart)

set(SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

if (CONFIG_ENABLE_AUDIO_TIMESYNC)
    if (CONFIG_SOC_COMPATIBLE_NRF52X)
        target_sources(app PRIVATE src/controller_time_nrf52.c)
    elseif (CONFIG_SOC_COMPATIBLE_NRF5340_CPUAPP)
        target_sources(app PRIVATE src/controller_time_nrf53_app.c)
    elseif (CONFIG_SOC_SERIES_NRF54LX OR CONFIG_SOC_SERIES_NRF54HX)
        target_sources(app PRIVATE src/controller_time_nrf54.c)
    else()
        MESSAGE(FATAL_ERROR "Unsupported series")
    endif()
endif()

target_sources(app PRIVATE ${SRCS})
