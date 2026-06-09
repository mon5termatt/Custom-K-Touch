/* Host shim for esp_attr.h — placement attributes are no-ops off-target. */
#ifndef PT_SIM_ESP_ATTR_H
#define PT_SIM_ESP_ATTR_H
#define EXT_RAM_BSS_ATTR
#define EXT_RAM_ATTR
#define IRAM_ATTR
#define DRAM_ATTR
#define WORD_ALIGNED_ATTR
#endif
