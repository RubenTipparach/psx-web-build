/*
 * BIOS helper functions for HLE BIOS compatibility
 * Based on psyqo's kernel.cpp implementation
 */

#include <stdint.h>
#include <stdio.h>
#include "bios.h"
#include "ps1/registers.h"

/* BIOS syscall wrappers - these call through the BIOS jump tables */

static inline void syscall_setDefaultExceptionJmpBuf(void) {
	register int n __asm__("t1") = 0x18;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	((void (*)(void))0xb0)();
}

static inline int syscall_enqueueSyscallHandler(int priority) {
	register int n __asm__("t1") = 0x01;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	return ((int (*)(int))0xc0)(priority);
}

static inline int syscall_enqueueIrqHandler(int priority) {
	register int n __asm__("t1") = 0x0c;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	return ((int (*)(int))0xc0)(priority);
}

static inline int syscall_enqueueRCntIrqs(int priority) {
	register int n __asm__("t1") = 0x00;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	return ((int (*)(int))0xc0)(priority);
}

static inline uint32_t syscall_openEvent(uint32_t classId, uint32_t spec, uint32_t mode, void (*handler)(void)) {
	register int n __asm__("t1") = 0x08;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	return ((uint32_t (*)(uint32_t, uint32_t, uint32_t, void (*)(void)))0xb0)(classId, spec, mode, handler);
}

static inline int syscall_enableEvent(uint32_t event) {
	register int n __asm__("t1") = 0x0c;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	return ((int (*)(uint32_t))0xb0)(event);
}

static inline void syscall_flushCache(void) {
	register int n __asm__("t1") = 0x44;
	__asm__ volatile("" : "=r"(n) : "r"(n));
	((void (*)(void))0xa0)();
}

/* Event constants from psyqo */
#define EVENT_DMA         0xf0000011
#define EVENT_MODE_CALLBACK 0x1000

/* DMA IRQ handler - called by BIOS when DMA completes */
/* This matches psyqo's dmaIRQ() in kernel.cpp exactly */
static void dmaIRQ(void) {
	/* Clear DMA IRQ flag in IRQ_STAT */
	IRQ_STAT = ~(1 << IRQ_DMA);

	/* Read DICR and acknowledge any pending DMA IRQs */
	uint32_t dicr = DMA_DICR;
	uint32_t dirqs = dicr >> 24;  /* IRQ status bits in upper byte */

	/* Preserve lower 24 bits (except bit 15 which is bus error, read-only) */
	dicr &= 0xff7fff;

	/* Build acknowledgment mask for triggered channels */
	uint32_t ack = 0x80;  /* Keep master enable bit */
	for (unsigned dma = 0; dma < 7; dma++) {
		uint32_t mask = 1 << dma;
		if (dirqs & mask) {
			ack |= mask;  /* Acknowledge this channel's IRQ */
		}
	}

	/* Write back to acknowledge IRQs */
	ack <<= 24;
	dicr |= ack;
	DMA_DICR = dicr;
}

void biosInit(void) {
	/* DISABLED - these BIOS syscalls crash pcsx_rearmed HLE */
	printf("BIOS: Skipped (HLE incompatible)\n");

#if 0
	/* Flush instruction cache first */
	syscall_flushCache();

	/* Set up BIOS exception handling - matches psyqo kernel.cpp */
	syscall_setDefaultExceptionJmpBuf();
	syscall_enqueueSyscallHandler(0);
	syscall_enqueueIrqHandler(3);
	syscall_enqueueRCntIrqs(1);

	/* Open DMA event with callback handler - CRITICAL for HLE BIOS */
	uint32_t event = syscall_openEvent(EVENT_DMA, 0x1000, EVENT_MODE_CALLBACK, dmaIRQ);
	syscall_enableEvent(event);

	/* Enable DMA interrupts in hardware */
	IRQ_MASK |= (1 << IRQ_DMA);

	/* Enable master DMA IRQ in DICR */
	uint32_t dicr = DMA_DICR;
	dicr &= 0xffffff;   /* Clear any pending IRQs */
	dicr |= 0x800000;   /* Set master IRQ enable */
	DMA_DICR = dicr;

	printf("BIOS: Events registered, DICR=0x%08lX\n", (unsigned long)DMA_DICR);
#endif
}

uint32_t biosOpenEvent(uint32_t classId, uint32_t spec, uint32_t mode, void (*handler)(void)) {
	return syscall_openEvent(classId, spec, mode, handler);
}

int biosEnableEvent(uint32_t event) {
	return syscall_enableEvent(event);
}

void biosFlushCache(void) {
	syscall_flushCache();
}
