/*
 * CD-DA Audio Playback for PS1 bare-metal
 * Simplified to match psyqo's approach exactly
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "cdda.h"
#include "ps1/registers.h"

/* CD-ROM commands (matching psyqo's CDL enum) */
#define CDL_SETLOC   0x02
#define CDL_PLAY     0x03
#define CDL_STOP     0x08
#define CDL_PAUSE    0x09
#define CDL_INIT     0x0A
#define CDL_DEMUTE   0x0C
#define CDL_SETMODE  0x0E
#define CDL_GETTD    0x14
#define CDL_SEEKP    0x16

/* Track position storage */
static uint8_t trackMinute[100];
static uint8_t trackSecond[100];
static int numTracks = 0;
static int currentTrack = 0;
static bool cddaInitialized = false;
static bool isPlaying = false;

/* Simple delay */
static void delay(int count) {
	for (volatile int i = 0; i < count; i++)
		__asm__ volatile("");
}

/* Wait for CD-ROM ready */
static void waitReady(void) {
	while (CDROM_HSTS & CDROM_HSTS_BUSYSTS)
		__asm__ volatile("");
}

/* Wait for interrupt and read response */
static int waitResponse(uint8_t *response, int maxLen) {
	/* Wait for interrupt */
	CDROM_ADDRESS = 1;
	while (!(CDROM_HINTSTS & CDROM_HINT_INT_BITMASK))
		__asm__ volatile("");

	int intType = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;

	/* Read response bytes */
	int count = 0;
	while ((CDROM_HSTS & CDROM_HSTS_RSLRRDY) && count < maxLen) {
		if (response) response[count] = CDROM_RESULT;
		else (void)CDROM_RESULT;
		count++;
	}

	/* Drain remaining */
	while (CDROM_HSTS & CDROM_HSTS_RSLRRDY)
		(void)CDROM_RESULT;

	/* Acknowledge interrupt */
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK;
	delay(1000);

	return intType;
}

/* Send command with no params */
static void sendCommand(uint8_t cmd) {
	waitReady();
	CDROM_ADDRESS = 0;
	CDROM_COMMAND = cmd;
	delay(1000);
}

/* Send command with params */
static void sendCommandParams(uint8_t cmd, const uint8_t *params, int numParams) {
	waitReady();
	CDROM_ADDRESS = 0;
	for (int i = 0; i < numParams; i++) {
		CDROM_PARAMETER = params[i];
	}
	CDROM_COMMAND = cmd;
	delay(1000);
}

/* BCD conversion */
static uint8_t toBCD(int val) {
	return ((val / 10) << 4) | (val % 10);
}

static int fromBCD(uint8_t bcd) {
	return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/* Set CD audio volume (matches psyqo's setVolume) */
static void setCDVolume(uint8_t leftToLeft, uint8_t rightToRight) {
	/* SPU CD audio input volume */
	SPU_AVOLL = 0x7FFF;
	SPU_AVOLR = 0x7FFF;

	/* CD-ROM mixer: Bank 2 */
	CDROM_ADDRESS = 2;
	CDROM_ATV0 = leftToLeft;   /* Left CD -> Left SPU */
	CDROM_ATV1 = 0;            /* Left CD -> Right SPU (no crossfeed) */

	/* CD-ROM mixer: Bank 3 */
	CDROM_ADDRESS = 3;
	CDROM_ATV2 = rightToRight; /* Right CD -> Right SPU */
	CDROM_ATV3 = 0;            /* Right CD -> Left SPU (no crossfeed) */
	CDROM_ADPCTL = 0x20;       /* Apply volume settings */
}

void initCDDA(void) {
	uint8_t response[8];

	printf("CDDA: Initializing...\n");

	/* Enable SPU with CD audio input */
	SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_DAC_ENABLE | SPU_CTRL_I2SA_ENABLE;
	delay(5000);

	/* Set master volume */
	SPU_MVOLL = 0x3FFF;
	SPU_MVOLR = 0x3FFF;

	/* Set CD volume (full stereo) */
	setCDVolume(0x80, 0x80);
	printf("CDDA: Volume set\n");

	/* Clear pending interrupts */
	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK | CDROM_HCLRCTL_CLRPRM;
	delay(5000);

	/* Initialize CD-ROM */
	printf("CDDA: Sending INIT...\n");
	sendCommand(CDL_INIT);
	waitResponse(response, 8);
	delay(50000);
	waitResponse(response, 8);  /* Init sends two responses */
	printf("CDDA: CD-ROM ready\n");

	/* Get track count (GETTD track 0 returns total tracks) */
	uint8_t param = 0;
	sendCommandParams(CDL_GETTD, &param, 1);
	waitResponse(response, 8);
	numTracks = fromBCD(response[2]);
	printf("CDDA: %d tracks on disc\n", numTracks);

	if (numTracks < 2) {
		printf("CDDA: No audio tracks\n");
		return;
	}

	/* Get position of track 2 (first audio track) */
	param = toBCD(2);
	sendCommandParams(CDL_GETTD, &param, 1);
	waitResponse(response, 8);
	trackMinute[2] = response[1];  /* Already BCD */
	trackSecond[2] = response[2];
	printf("CDDA: Track 2 at %02X:%02X:00\n", trackMinute[2], trackSecond[2]);

	/* Demute CD audio */
	sendCommand(CDL_DEMUTE);
	waitResponse(response, 8);
	printf("CDDA: Demuted\n");

	cddaInitialized = true;

	/* Start playing track 2 */
	playCDDATrack(2);
}

void playCDDATrack(int track) {
	uint8_t response[8];
	uint8_t params[3];

	if (!cddaInitialized) return;
	if (track < 2 || track > numTracks) {
		printf("CDDA: Invalid track %d\n", track);
		return;
	}

	printf("CDDA: Playing track %d...\n", track);

	/* Get track position if not already cached */
	if (trackMinute[track] == 0 && trackSecond[track] == 0) {
		uint8_t param = toBCD(track);
		sendCommandParams(CDL_GETTD, &param, 1);
		waitResponse(response, 8);
		trackMinute[track] = response[1];
		trackSecond[track] = response[2];
	}

	/* SETMODE: 0x01 = CD-DA audio output enabled */
	params[0] = 0x01;
	sendCommandParams(CDL_SETMODE, params, 1);
	waitResponse(response, 8);

	/* SETLOC: Set position to track start (BCD format) */
	params[0] = trackMinute[track];
	params[1] = trackSecond[track];
	params[2] = 0x00;  /* Frame 0 */
	sendCommandParams(CDL_SETLOC, params, 3);
	waitResponse(response, 8);

	/* SEEKP: Seek to audio position */
	sendCommand(CDL_SEEKP);
	waitResponse(response, 8);  /* Acknowledge */
	waitResponse(response, 8);  /* Complete */
	printf("CDDA: Seek complete\n");

	/* PLAY: Start playback */
	sendCommand(CDL_PLAY);
	waitResponse(response, 8);

	currentTrack = track;
	isPlaying = true;
	printf("CDDA: Playing!\n");
}

void stopCDDA(void) {
	uint8_t response[8];
	if (!cddaInitialized) return;

	sendCommand(CDL_STOP);
	waitResponse(response, 8);
	isPlaying = false;
	printf("CDDA: Stopped\n");
}

void pauseCDDA(void) {
	uint8_t response[8];
	if (!cddaInitialized) return;

	sendCommand(CDL_PAUSE);
	waitResponse(response, 8);
	isPlaying = false;
	printf("CDDA: Paused\n");
}

bool isCDDAPlaying(void) {
	return isPlaying;
}

void updateCDDA(void) {
	/* Simple implementation - no looping for now */
	/* In a real game, you'd check for track end and restart */
}
