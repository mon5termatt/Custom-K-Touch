/* Prusa-Touch — network log pipe. See netlog.h. */
#include "netlog.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define NETLOG_CAP 65536   /* fixed PSRAM ring — bounded device memory; host keeps full history.
                            * 64 KB gives headroom for bursts (e.g. a panic backtrace) between polls. */

static char            *s_ring;
static uint32_t         s_head;     /* total bytes ever written (monotonic sequence) */
static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t   s_orig;     /* original (UART) vprintf, chained so serial still works */

/* Both helpers run inside the critical section. Wrap-aware memcpy keeps it to ~2 copies. */
static void ring_write(const char *src, int len)
{
    int pos = (int)(s_head % NETLOG_CAP);
    int first = NETLOG_CAP - pos; if (first > len) first = len;
    memcpy(s_ring + pos, src, first);
    if (len > first) memcpy(s_ring, src + first, len - first);
    s_head += (uint32_t)len;
}

static int netlog_vprintf(const char *fmt, va_list ap)
{
    if (s_ring) {
        char line[200];
        va_list ap2; va_copy(ap2, ap);
        int n = vsnprintf(line, sizeof(line), fmt, ap2);
        va_end(ap2);
        if (n > 0) {
            int len = n < (int)sizeof(line) ? n : (int)sizeof(line) - 1;
            portENTER_CRITICAL(&s_mux);
            ring_write(line, len);
            portEXIT_CRITICAL(&s_mux);
        }
    }
    return s_orig ? s_orig(fmt, ap) : 0;   /* keep printing to UART */
}

void netlog_init(void)
{
    if (s_ring) return;
    s_ring = heap_caps_malloc(NETLOG_CAP, MALLOC_CAP_SPIRAM);
    if (!s_ring) return;
    s_orig = esp_log_set_vprintf(netlog_vprintf);
}

size_t netlog_read(uint32_t since, char *out, size_t outcap, uint32_t *head, uint32_t *oldest)
{
    if (!s_ring) { if (head) *head = 0; if (oldest) *oldest = 0; return 0; }
    portENTER_CRITICAL(&s_mux);
    uint32_t h   = s_head;
    uint32_t old = (h > NETLOG_CAP) ? (h - NETLOG_CAP) : 0;
    uint32_t from = since;
    if (from < old) from = old;
    if (from > h)   from = h;
    size_t avail = h - from;
    if (avail > outcap) { from = h - (uint32_t)outcap; avail = outcap; }   /* newest outcap bytes */
    int pos = (int)(from % NETLOG_CAP);
    size_t first = NETLOG_CAP - pos; if (first > avail) first = avail;
    memcpy(out, s_ring + pos, first);
    if (avail > first) memcpy(out + first, s_ring, avail - first);
    portEXIT_CRITICAL(&s_mux);
    if (head) *head = h;
    if (oldest) *oldest = old;
    return avail;
}
