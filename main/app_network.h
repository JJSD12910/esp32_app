#ifndef APP_NETWORK_H
#define APP_NETWORK_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ensure Wi-Fi is connected (STA) and wait until ready.
 *
 * Initializes Wi-Fi on first call. Safe to call multiple times.
 *
 * @param timeout_ticks How long to wait for connection (FreeRTOS ticks).
 * @return true if connected before timeout, false otherwise.
 */
bool app_network_wait_for_wifi(TickType_t timeout_ticks);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // APP_NETWORK_H
