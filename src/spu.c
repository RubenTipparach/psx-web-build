/*
 * SPU helper functions for PS1 bare-metal
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "spu.h"
#include "ps1/registers.h"

/* SPU RAM starts at 0x1000 (first 4KB reserved for capture buffers) */
#define SPU_RAM_START 0x1000
#define SPU_RAM_END   0x80000  /* 512KB total SPU RAM */

/* Current position in SPU RAM for uploads */
static uint32_t spuRamPos = SPU_RAM_START;

/* Small delay for SPU operations */
static void spuDelay(void) {
	for (volatile int i = 0; i < 100; i++)
		__asm__ volatile("");
}

void setupSPU(void) {
	/* Enable DMA channel 4 (SPU) */
	DMA_DPCR |= DMA_DPCR_CH_ENABLE(DMA_SPU);

	/* Reset SPU */
	SPU_CTRL = 0;
	spuDelay();

	/* Wait for SPU to be ready (with timeout for HLE BIOS compatibility) */
	for (int timeout = 10000; timeout > 0 && (SPU_STAT & SPU_STAT_BUSY); timeout--)
		__asm__ volatile("");

	/* Turn off all channels first */
	SPU_KOFF0 = 0xFFFF;
	SPU_KOFF1 = 0x00FF;
	spuDelay();

	/* Set master volume (max is 0x3FFF) */
	SPU_MVOLL = 0x3FFF;
	SPU_MVOLR = 0x3FFF;

	/* Clear CD audio volume (not using CD audio) */
	SPU_AVOLL = 0;
	SPU_AVOLR = 0;

	/* Clear reverb volume */
	SPU_EVOLL = 0;
	SPU_EVOLR = 0;

	/* Enable SPU with DAC and unmute - use full control word */
	/* Bit 15: SPU enable, Bit 14: unmute/DAC enable */
	SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_DAC_ENABLE;
	spuDelay();

	/* Wait for SPU to stabilize */
	for (volatile int i = 0; i < 1000; i++)
		__asm__ volatile("");
}

uint32_t uploadVAG(const void *data, size_t size) {
	const uint8_t *src = (const uint8_t *)data;

	/* psxavenc -t spu outputs raw SPU-ADPCM data (no VAG header) */

	/* Cap size to available SPU RAM */
	size_t maxSize = SPU_RAM_END - spuRamPos;
	if (size > maxSize) {
		size = maxSize;
	}

	/* Align size to 64 bytes (SPU DMA requirement) */
	size_t alignedSize = (size + 63) & ~63;

	/* Store the starting address */
	uint32_t spuAddr = spuRamPos;

	/* Set transfer address (in 8-byte units) */
	SPU_TSA = spuAddr >> 3;

	/* Enable DMA write mode */
	SPU_CTRL = (SPU_CTRL & ~SPU_CTRL_XFER_BITMASK) | SPU_CTRL_XFER_DMA_WRITE;
	spuDelay();

	/* Wait for SPU to be ready (with timeout) */
	for (int t = 0; t < 10000 && !(SPU_STAT & SPU_STAT_DREQ); t++)
		__asm__ volatile("");

	/* Set up DMA transfer */
	DMA_MADR(DMA_SPU) = (uint32_t)src;
	DMA_BCR(DMA_SPU) = ((alignedSize / 64) << 16) | 16;  /* 16 words per block */
	DMA_CHCR(DMA_SPU) = DMA_CHCR_WRITE | DMA_CHCR_MODE_SLICE | DMA_CHCR_ENABLE;

	/* Wait for DMA to complete (with generous timeout for emulators) */
	for (int t = 0; t < 1000000 && (DMA_CHCR(DMA_SPU) & DMA_CHCR_ENABLE); t++)
		__asm__ volatile("");

	/* Reset transfer mode */
	SPU_CTRL = (SPU_CTRL & ~SPU_CTRL_XFER_BITMASK) | SPU_CTRL_XFER_NONE;
	spuDelay();

	/* Update RAM position for next upload */
	spuRamPos += alignedSize;

	return spuAddr;
}

void playSample(int channel, uint32_t spuAddr, int sampleRate, int volume) {
	/* Calculate pitch: pitch = (sampleRate * 4096) / 44100 */
	int pitch = (sampleRate * 4096) / 44100;
	if (pitch > 0x3FFF) pitch = 0x3FFF;

	/* Clamp volume */
	if (volume > 0x3FFF) volume = 0x3FFF;

	printf("SPU: Playing ch%d addr=0x%04X pitch=%d vol=%d\n",
		channel, (unsigned)(spuAddr >> 3), pitch, volume);

	/* Set channel parameters */
	SPU_CH_VOLL(channel) = volume;
	SPU_CH_VOLR(channel) = volume;
	SPU_CH_PITCH(channel) = pitch;
	SPU_CH_SSA(channel) = spuAddr >> 3;  /* Start address in 8-byte units */
	SPU_CH_LSAX(channel) = spuAddr >> 3; /* Loop address = start (for looping samples) */

	/* Set ADSR for sustained playback:
	 * ADSR1: Sustain level = 15 (max), Decay = 0, Attack = fastest
	 * ADSR2: Sustain holds, Release = slowest */
	SPU_CH_ADSR1(channel) = 0x00FF;  /* Attack max, Decay 0, Sustain 15 */
	SPU_CH_ADSR2(channel) = 0x0000;  /* Sustain mode, no release */

	/* Trigger the channel (key on) */
	printf("SPU: Triggering channel %d\n", channel);
	if (channel < 16) {
		SPU_KON0 = 1 << channel;
	} else {
		SPU_KON1 = 1 << (channel - 16);
	}

	/* Small delay after key on */
	for (volatile int i = 0; i < 100; i++)
		__asm__ volatile("");

	printf("SPU: Channel triggered, CTRL=0x%04X STAT=0x%04X\n", SPU_CTRL, SPU_STAT);
}

void stopChannel(int channel) {
	if (channel < 16) {
		SPU_KOFF0 = 1 << channel;
	} else {
		SPU_KOFF1 = 1 << (channel - 16);
	}
}
