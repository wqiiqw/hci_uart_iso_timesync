/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

// BK: based on / taken from ncs/nrf/samples/bluetooth/conn_time_sync/src/controller_time_sync.h

#ifndef CONTROLLER_TIME_SYNC_H__
#define CONTROLLER_TIME_SYNC_H__

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

/** @brief Obtain the current Bluetooth controller time.
 *
 * The timestamps are based upon this clock.
 *
 * @return The current controller time.
 */
uint64_t controller_time_us_get(void);

/** @brief Set the controller to trigger a PPI event at the given timestamp.
 *
 * @param timestamp_us The timestamp where it will trigger.
 */
void controller_time_trigger_set(uint64_t timestamp_us);

/** @brief Get the address of the event that will trigger.
 *
 * @return The address of the event that will trigger.
 */
uint32_t controller_time_trigger_event_addr_get(void);

#endif

/**
 * @}
 */
