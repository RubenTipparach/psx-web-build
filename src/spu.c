/*
 * SPU helper functions - matching psyqo modplayer EXACTLY
 * Source: psyqo/modplayer/modplayer.c
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "spu.h"
#include "ps1/registers.h"

/* SPU RAM layout - matching psyqo */
#define SPU_RAM_START 0x1010

static uint32_t spuRamPos = SPU_RAM_START;

/* psyqo register definitions (from common/hardware/spu.h) */
#define SPU_VOICES ((volatile struct SPUVoice *)0x1f801c00)
#define SPU_VOL_MAIN_LEFT   (*(volatile uint16_t *)0x1f801d80)
#define SPU_VOL_MAIN_RIGHT  (*(volatile uint16_t *)0x1f801d82)
#define SPU_KEY_ON_LOW      (*(volatile uint16_t *)0x1f801d88)
#define SPU_KEY_ON_HIGH     (*(volatile uint16_t *)0x1f801d8a)
#define SPU_KEY_OFF_LOW     (*(volatile uint16_t *)0x1f801d8c)
#define SPU_KEY_OFF_HIGH    (*(volatile uint16_t *)0x1f801d8e)
#define SPU_PITCH_MOD_LOW   (*(volatile uint16_t *)0x1f801d90)
#define SPU_PITCH_MOD_HIGH  (*(volatile uint16_t *)0x1f801d92)
#define SPU_NOISE_EN_LOW    (*(volatile uint16_t *)0x1f801d94)
#define SPU_NOISE_EN_HIGH   (*(volatile uint16_t *)0x1f801d96)
#define SPU_REVERB_EN_LOW   (*(volatile uint16_t *)0x1f801d98)
#define SPU_REVERB_EN_HIGH  (*(volatile uint16_t *)0x1f801d9a)
#define SPU_RAM_DTA         (*(volatile uint16_t *)0x1f801da6)
#define SPU_CTRL_REG        (*(volatile uint16_t *)0x1f801daa)
#define SPU_RAM_DTC         (*(volatile uint16_t *)0x1f801dac)
#define SPU_STATUS_REG      (*(volatile uint16_t *)0x1f801dae)
#define SPU_VOL_CD_LEFT     (*(volatile uint16_t *)0x1f801db0)
#define SPU_VOL_CD_RIGHT    (*(volatile uint16_t *)0x1f801db2)
#define SPU_VOL_EXT_LEFT    (*(volatile uint16_t *)0x1f801db4)
#define SPU_VOL_EXT_RIGHT   (*(volatile uint16_t *)0x1f801db6)

/* DMA registers - matching psyqo */
#define DPCR (*(volatile uint32_t *)0x1f8010f0)
#define SBUS_DEV4_CTRL (*(volatile uint32_t *)0x1f801014)

struct SPUVoice {
	uint16_t volumeLeft;
	uint16_t volumeRight;
	uint16_t sampleRate;
	uint16_t sampleStartAddr;
	uint16_t ad;
	uint16_t sr;
	uint16_t currentVolume;
	uint16_t sampleRepeatAddr;
};

struct DMARegisters {
	uintptr_t MADR;
	uint32_t BCR, CHCR, padding;
};

#define DMA_CTRL ((volatile struct DMARegisters *)0x1f801080)
#define DMA_SPU 4

/*
 * SPUWaitIdle - matching psyqo with timeout for safety
 */
static void SPUWaitIdle(void) {
	int timeout = 10000;
	while ((SPU_STATUS_REG & 0x07ff) != 0 && --timeout > 0) {
		for (volatile int c = 0; c < 100; c++);
	}
}

/*
 * SPUResetVoice - matching psyqo exactly
 */
static void SPUResetVoice(int voiceID) {
	SPU_VOICES[voiceID].volumeLeft = 0;
	SPU_VOICES[voiceID].volumeRight = 0;
	SPU_VOICES[voiceID].sampleRate = 0;
	SPU_VOICES[voiceID].sampleStartAddr = 0;
	SPU_VOICES[voiceID].ad = 0x000f;
	SPU_VOICES[voiceID].currentVolume = 0;
	SPU_VOICES[voiceID].sampleRepeatAddr = 0;
	SPU_VOICES[voiceID].sr = 0x0000;
}

/*
 * SPUInit - matching psyqo modplayer exactly
 */
void setupSPU(void) {
	printf("SPU: Init (psyqo modplayer)...\n");

	/* Enable DMA for SPU - EXACTLY like psyqo */
	DPCR |= 0x000b0000;

	SPU_VOL_MAIN_LEFT = 0x3800;
	SPU_VOL_MAIN_RIGHT = 0x3800;
	SPU_CTRL_REG = 0;
	SPU_KEY_ON_LOW = 0;
	SPU_KEY_ON_HIGH = 0;
	SPU_KEY_OFF_LOW = 0xffff;
	SPU_KEY_OFF_HIGH = 0xffff;
	SPU_RAM_DTC = 4;
	SPU_VOL_CD_LEFT = 0x7FFF;   /* CD audio volume - full */
	SPU_VOL_CD_RIGHT = 0x7FFF;
	SPU_PITCH_MOD_LOW = 0;
	SPU_PITCH_MOD_HIGH = 0;
	SPU_NOISE_EN_LOW = 0;
	SPU_NOISE_EN_HIGH = 0;
	SPU_REVERB_EN_LOW = 0;
	SPU_REVERB_EN_HIGH = 0;
	SPU_VOL_EXT_LEFT = 0;
	SPU_VOL_EXT_RIGHT = 0;
	SPU_CTRL_REG = 0x8000;

	/* Reset all 24 voices */
	for (int i = 0; i < 24; i++) {
		SPUResetVoice(i);
	}

	printf("SPU: Ready, CTRL=0x%04X STAT=0x%04X\n", SPU_CTRL_REG, SPU_STATUS_REG);
}

/*
 * SPUUnMute - enable SPU with CD audio input
 */
void spuUnmute(void) {
	/* 0xC001 = Enable (0x8000) + DAC (0x4000) + CD Audio Input (0x0001) */
	SPU_CTRL_REG = 0xc001;

	/* Also set CD audio volume */
	SPU_VOL_CD_LEFT = 0x7FFF;
	SPU_VOL_CD_RIGHT = 0x7FFF;
}

/*
 * Upload SPU-ADPCM data to SPU RAM
 * Note: Our data is raw SPU-ADPCM (no VAG header) from psxavenc -t spu
 */
uint32_t uploadVAG(const void *data, size_t size) {
	const uint8_t *spuData = (const uint8_t *)data;
	size_t spuSize = size;

	uint32_t addr = spuRamPos;

	printf("SPU: Upload %u bytes to 0x%05lX\n", (unsigned)spuSize, (unsigned long)addr);

	/* Calculate BCR - matching psyqo exactly */
	uint32_t bcr = spuSize >> 6;
	if (spuSize & 0x3f) bcr++;
	bcr <<= 16;
	bcr |= 0x10;

	/* Set transfer address */
	SPU_RAM_DTA = addr >> 3;

	/* Set DMA write mode - matching psyqo exactly */
	SPU_CTRL_REG = (SPU_CTRL_REG & ~0x0030) | 0x0020;
	{
		int timeout = 10000;
		while ((SPU_CTRL_REG & 0x0030) != 0x0020 && --timeout > 0);
	}

	/* Note: psyqo does SBUS_DEV4_CTRL &= ~0x0f000000 here but that may
	 * cause issues with HLE BIOS, so we skip it */

	/* DMA transfer - matching psyqo exactly */
	DMA_CTRL[DMA_SPU].MADR = (uint32_t)spuData;
	DMA_CTRL[DMA_SPU].BCR = bcr;
	DMA_CTRL[DMA_SPU].CHCR = 0x01000201;

	/* Wait for DMA completion */
	{
		int timeout = 100000;
		while ((DMA_CTRL[DMA_SPU].CHCR & 0x01000000) != 0 && --timeout > 0);
	}

	/* Update allocation pointer */
	spuRamPos = addr + ((spuSize + 63) & ~63);

	printf("SPU: Done\n");
	return addr;
}

/*
 * Play sample - fast version without debug output
 */
void playSample(int channel, uint32_t spuAddr, int sampleRate, int volume) {
	/* Calculate pitch - matching psyqo formula */
	int pitch = (sampleRate << 12) / 44100;
	if (pitch > 0x3FFF) pitch = 0x3FFF;
	if (pitch < 1) pitch = 1;

	/* Volume scaling */
	if (volume > 0x3FFF) volume = 0x3FFF;

	/* Set voice parameters */
	SPU_VOICES[channel].volumeLeft = volume;
	SPU_VOICES[channel].volumeRight = volume;
	SPU_VOICES[channel].sampleStartAddr = spuAddr >> 3;

	/* Key on immediately - no wait needed for sound effects */
	if (channel < 16) {
		SPU_KEY_ON_LOW = (1 << channel);
	} else {
		SPU_KEY_ON_HIGH = (1 << (channel - 16));
	}

	/* Set sample rate after key on */
	SPU_VOICES[channel].sampleRate = pitch;
}

void stopChannel(int channel) {
	if (channel < 16) {
		SPU_KEY_OFF_LOW = (1 << channel);
	} else {
		SPU_KEY_OFF_HIGH = (1 << (channel - 16));
	}
}
