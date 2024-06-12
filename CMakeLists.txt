# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(hci_uart)

set(SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c
)

if (CONFIG_AUDIO_SYNC_TIMER_USES_RTC)
    list(APPEND SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_sync_timer_rtc.c)
endif()

target_sources(app PRIVATE ${SRCS})
