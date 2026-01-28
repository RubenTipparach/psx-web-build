/*
 * BIOS helper functions for HLE BIOS compatibility
 * Based on psyqo's syscall implementation
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Event class IDs */
#define EVENT_DMA       0xf0000011

/* Event modes */
#define EVENT_MODE_CALLBACK  0x1000

/* Initialize BIOS event system for HLE compatibility */
void biosInit(void);

/* Open a BIOS event */
uint32_t biosOpenEvent(uint32_t classId, uint32_t spec, uint32_t mode, void (*handler)(void));

/* Enable a BIOS event */
int biosEnableEvent(uint32_t event);

/* Flush instruction cache */
void biosFlushCache(void);

#ifdef __cplusplus
}
#endif
