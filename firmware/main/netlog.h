#pragma once
/* Prusa-Touch — network log pipe.
 *
 * Captures the ESP-IDF console (every ESP_LOGx) into a PSRAM ring buffer while still teeing to
 * UART, so the device's logs can be read over WiFi via GET /api/log?since=<seq> — a "serial over
 * the network" for headless debugging. */
#include <stdint.h>
#include <stddef.h>

void   netlog_init(void);   /* install the log hook (call once, early in app_main) */

/* Copy log bytes from sequence `since` to the newest into out (up to outcap). Returns bytes copied;
 * *head = current write sequence (use as the next `since`); *oldest = oldest seq still buffered
 * (if your `since` was below it, you missed some — there was a gap). */
size_t netlog_read(uint32_t since, char *out, size_t outcap, uint32_t *head, uint32_t *oldest);
