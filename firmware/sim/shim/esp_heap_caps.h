/* Host shim for esp_heap_caps.h — map PSRAM/internal allocation to the C library. */
#ifndef PT_SIM_ESP_HEAP_CAPS_H
#define PT_SIM_ESP_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_DMA      0
#define MALLOC_CAP_8BIT     0
#define MALLOC_CAP_DEFAULT  0
static inline void *heap_caps_malloc(size_t sz, unsigned caps) { (void)caps; return malloc(sz); }
static inline void *heap_caps_realloc(void *p, size_t sz, unsigned caps) { (void)caps; return realloc(p, sz); }
static inline void  heap_caps_free(void *p) { free(p); }
static inline size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 8u*1024*1024; }
static inline size_t heap_caps_get_minimum_free_size(unsigned caps) { (void)caps; return 8u*1024*1024; }
#endif
