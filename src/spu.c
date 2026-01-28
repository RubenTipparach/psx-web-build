/*
 * SPU helper functions - matching PSn00bSDK libpsxspu EXACTLY
 * Source: https://github.com/Lameguy64/PSn00bSDK/blob/master/libpsn00b/psxspu/common.c
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "spu.h"
#include "ps1/registers.h"

/* SPU RAM layout - matching PSn00bSDK */
#define WRITABLE_AREA_ADDR  0x200   /* 0x1000 >> 3 = 0x200 in 8-byte units */
#define SPU_RAM_START       0x1010  /* After dummy block */

static uint32_t spuRamPos = SPU_RAM_START;
static int transferMode = 0;  /* 0 = DMA, 1 = manual */
static uint32_t transferAddr = WRITABLE_AREA_ADDR;

/* Dummy looping ADPCM block - required for SPU init */
static const uint16_t dummyBlock[8] = {
	0x0500, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
};

/* Wait for SPU status - matching PSn00bSDK _wait_status() */
static void waitStatus(uint16_t mask, uint16_t value) {
	int timeout = 100000;
	while (((SPU_STAT & mask) != value) && --timeout > 0);
}

/* Manual write to SPU RAM - matching PSn00bSDK _manual_write() */
static size_t manualWrite(const uint16_t *data, size_t size) {
	/* Set transfer address */
	SPU_TSA = transferAddr;

	/* Set manual write mode */
	uint16_t ctrl = SPU_CTRL & ~0x0030;
	ctrl |= 0x0010;  /* Manual write */
	SPU_CTRL = ctrl;
	waitStatus(0x0030, 0x0010);

	/* Write data */
	size_t words = (size + 1) / 2;
	for (size_t i = 0; i < words; i++) {
		SPU_DATA = data[i];
	}

	/* Wait for completion */
	waitStatus(0x0400, 0x0000);

	/* Update transfer address */
	transferAddr += words;

	return words;
}

/* DMA transfer - matching PSn00bSDK _dma_transfer() */
static size_t dmaTransfer(uint32_t *data, size_t size, int write) {
	/* Calculate BCR */
	size_t length = size / 4;
	length = (length + 3) / 4;  /* Round up to 4-word chunks */

	/* Set transfer address */
	SPU_TSA = transferAddr;

	/* Set DMA write mode */
	uint16_t ctrl = SPU_CTRL & ~0x0030;
	ctrl |= 0x0020;  /* DMA write */
	SPU_CTRL = ctrl;
	waitStatus(0x0030, 0x0020);

	/* Configure DMA */
	DMA_MADR(DMA_SPU) = (uint32_t)data;
	DMA_BCR(DMA_SPU) = 4 | (length << 16);
	DMA_CHCR(DMA_SPU) = 0x01000201;  /* Start transfer */

	/* Wait for DMA completion */
	int timeout = 100000;
	while ((DMA_CHCR(DMA_SPU) & 0x01000000) && --timeout > 0);

	/* Update transfer address */
	transferAddr += length * 4;

	return length * 4;
}

/*
 * SpuInit - EXACT copy from PSn00bSDK
 */
void setupSPU(void) {
	printf("SPU: Init (PSn00bSDK)...\n");

	/* CRITICAL: Set SPU bus timing - this is what makes it work! */
	BIU_DEV4_CTRL = 0x200931e1;

	/* Disable SPU and wait */
	SPU_CTRL = 0x0000;
	waitStatus(0x001f, 0x0000);

	/* Clear all volumes */
	SPU_MVOLL = 0;
	SPU_MVOLR = 0;
	SPU_EVOLL = 0;
	SPU_EVOLR = 0;

	/* Key off all voices */
	SPU_KOFF0 = 0xffff;
	SPU_KOFF1 = 0x00ff;

	/* Disable modulation, noise, reverb */
	SPU_PMON0 = 0;
	SPU_PMON1 = 0;
	SPU_NON0 = 0;
	SPU_NON1 = 0;
	SPU_EON0 = 0;
	SPU_EON1 = 0;

	/* Set reverb address to end of RAM */
	SPU_ESA = 0xfffe;

	/* Clear CD and external volumes */
	SPU_AVOLL = 0;
	SPU_AVOLR = 0;
	SPU_BVOLL = 0;
	SPU_BVOLR = 0;

	/* Enable SPU DMA channel */
	DMA_DPCR |= DMA_DPCR_CH_PRIORITY(DMA_SPU, 3) | DMA_DPCR_CH_ENABLE(DMA_SPU);
	DMA_CHCR(DMA_SPU) = 0x00000201;

	/* Reset transfer control */
	SPU_FIFO_CTRL = 0x0004;

	/* Enable SPU with CD audio - NOTE: 0xc001 not 0xc000! */
	SPU_CTRL = 0xc001;
	waitStatus(0x003f, 0x0001);

	/* Upload dummy block to start of writable SPU RAM */
	transferAddr = WRITABLE_AREA_ADDR;
	manualWrite(dummyBlock, sizeof(dummyBlock));

	/* Initialize all 24 voices to play dummy block */
	for (int i = 0; i < 24; i++) {
		SPU_CH_VOLL(i) = 0;
		SPU_CH_VOLR(i) = 0;
		SPU_CH_PITCH(i) = 0x1000;  /* 44100 Hz */
		SPU_CH_SSA(i) = WRITABLE_AREA_ADDR;
	}

	/* Key on ALL voices (playing silent dummy) */
	SPU_KON0 = 0xffff;
	SPU_KON1 = 0x00ff;

	/* Set master volume */
	SPU_MVOLL = 0x3fff;
	SPU_MVOLR = 0x3fff;

	/* Set CD audio volume */
	SPU_AVOLL = 0x7fff;
	SPU_AVOLR = 0x7fff;

	printf("SPU: Ready, CTRL=0x%04X STAT=0x%04X\n", SPU_CTRL, SPU_STAT);
}

void spuUnmute(void) {
	/* Already enabled in setupSPU */
}

/*
 * SpuSetTransferStartAddr
 */
static uint32_t spuSetTransferStartAddr(uint32_t addr) {
	transferAddr = addr >> 3;
	return addr;
}

/*
 * SpuWrite - matching PSn00bSDK
 */
uint32_t uploadVAG(const void *data, size_t size) {
	uint32_t addr = spuRamPos;

	printf("SPU: Upload %u bytes to 0x%05lX\n", (unsigned)size, (unsigned long)addr);

	/* Set transfer address */
	spuSetTransferStartAddr(addr);

	/* Use DMA transfer (mode 0) */
	dmaTransfer((uint32_t *)data, size, 1);

	/* Wait for completion */
	waitStatus(0x0400, 0x0000);

	/* Update allocation pointer */
	spuRamPos = addr + ((size + 63) & ~63);

	printf("SPU: Done\n");
	return addr;
}

/*
 * Play sample - matching PSn00bSDK voice setup
 */
void playSample(int channel, uint32_t spuAddr, int sampleRate, int volume) {
	int pitch = (sampleRate * 4096) / 44100;
	if (pitch > 0x3FFF) pitch = 0x3FFF;
	if (pitch < 1) pitch = 1;
	if (volume > 0x3FFF) volume = 0x3FFF;

	printf("SPU: Play ch%d addr=0x%04lX\n", channel, (unsigned long)(spuAddr >> 3));

	/* Key off first */
	if (channel < 16) {
		SPU_KOFF0 = (1 << channel);
	} else {
		SPU_KOFF1 = (1 << (channel - 16));
	}

	/* Small delay after key off */
	for (volatile int i = 0; i < 100; i++);

	/* Set voice parameters - matching Snake's ADSR settings exactly */
	/* ADSR1: Am=0(linear), AR=0(instant), DR=0(instant), SL=F(max sustain) = 0x000F */
	/* ADSR2: Sm=0, Sd=0, SR=0, Rm=0, RR=0 = 0x0000 */
	SPU_CH_VOLL(channel) = volume;
	SPU_CH_VOLR(channel) = volume;
	SPU_CH_PITCH(channel) = pitch;
	SPU_CH_SSA(channel) = spuAddr >> 3;
	SPU_CH_ADSR1(channel) = 0x000f;  /* SL=F, DR=0, AR=0 (instant on, sustain at max) */
	SPU_CH_ADSR2(channel) = 0x0000;  /* No release */

	/* Small delay before key on */
	for (volatile int i = 0; i < 100; i++);

	/* Key on */
	if (channel < 16) {
		SPU_KON0 = (1 << channel);
	} else {
		SPU_KON1 = (1 << (channel - 16));
	}
}

void stopChannel(int channel) {
	if (channel < 16) {
		SPU_KOFF0 = (1 << channel);
	} else {
		SPU_KOFF1 = (1 << (channel - 16));
	}
}
