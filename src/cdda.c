/*
 * CD-DA Audio Playback for PS1 bare-metal
 * Rewritten to match PSX Snake game's Audio.c approach exactly
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "cdda.h"
#include "ps1/registers.h"

/* CD-ROM commands - matching Snake's CdlXxx defines */
#define CdlNop       0x01
#define CdlSetloc    0x02
#define CdlPlay      0x03
#define CdlStop      0x08
#define CdlPause     0x09
#define CdlInit      0x0A
#define CdlDemute    0x0C
#define CdlSetmode   0x0E
#define CdlGetTN     0x13
#define CdlGetTD     0x14

/* Mode flags - matching Snake's CdlModeXxx */
#define CdlModeDA    0x01  /* CD-DA playback mode */
#define CdlModeRept  0x04  /* Report mode (sends play position) */

/* Status flags */
#define CdlStatPlay  0x80  /* Currently playing audio */

/* Track position storage - like Snake's CdlLOC loc[100] */
typedef struct {
	uint8_t minute;
	uint8_t second;
	uint8_t sector;
	uint8_t track;
} CdlLOC;

static CdlLOC loc[100];
static int ntoc = 0;

/* Looping control - matching Snake exactly */
static int loopMusic = 0;
static int trackToLoop = 2;
static int loopWait = 0;
static int currentTrack = 0;

/* Result buffer for CD commands */
static uint8_t result[8];

/* Small delay for CD-ROM operations */
static void cdDelay(void) {
	for (volatile int i = 0; i < 10000; i++)
		__asm__ volatile("");
}

/* Long delay for init operations */
static void cdLongDelay(void) {
	for (volatile int i = 0; i < 100000; i++)
		__asm__ volatile("");
}

/* Wait for CD-ROM controller to be ready */
static void waitCDReady(void) {
	while (CDROM_HSTS & CDROM_HSTS_BUSYSTS)
		__asm__ volatile("");
}

/* Wait for response and read result bytes */
static int waitForResponse(uint8_t *res, int maxBytes) {
	int timeout = 100000;

	/* Switch to bank 1 for interrupt status */
	CDROM_ADDRESS = 1;

	while (!(CDROM_HINTSTS & CDROM_HINT_INT_BITMASK) && timeout > 0) {
		timeout--;
		__asm__ volatile("");
	}

	int intType = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;

	/* Read response bytes */
	CDROM_ADDRESS = 1;
	int count = 0;
	while ((CDROM_HSTS & CDROM_HSTS_RSLRRDY) && count < maxBytes) {
		if (res) res[count] = CDROM_RESULT;
		else { volatile uint8_t dummy = CDROM_RESULT; (void)dummy; }
		count++;
	}
	/* Discard any remaining */
	while (CDROM_HSTS & CDROM_HSTS_RSLRRDY) {
		volatile uint8_t dummy = CDROM_RESULT;
		(void)dummy;
	}

	/* Acknowledge interrupt */
	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK;
	cdDelay();

	return intType;
}

/* Send command with no parameters */
static void CdControl0(uint8_t cmd) {
	waitCDReady();
	CDROM_ADDRESS = 0;
	CDROM_COMMAND = cmd;
	cdDelay();
}

/* Send command with parameters (like Snake's CdControl) */
static void CdControl(uint8_t cmd, uint8_t *param, int paramCount) {
	waitCDReady();
	CDROM_ADDRESS = 0;
	for (int i = 0; i < paramCount; i++) {
		CDROM_PARAMETER = param[i];
	}
	CDROM_COMMAND = cmd;
	cdDelay();
}

/* Get TOC - like Snake's CdGetToc(loc) */
static int CdGetToc(CdlLOC *locArray) {
	uint8_t res[8];

	/* GetTN - get first and last track numbers */
	CdControl0(CdlGetTN);
	waitForResponse(res, 8);

	int firstTrack = ((res[1] >> 4) * 10) + (res[1] & 0x0F);
	int lastTrack = ((res[2] >> 4) * 10) + (res[2] & 0x0F);

	printf("CDDA: TOC has tracks %d to %d\n", firstTrack, lastTrack);

	/* GetTD for each track */
	for (int t = 1; t <= lastTrack && t < 100; t++) {
		uint8_t trackBCD = ((t / 10) << 4) | (t % 10);
		uint8_t param[1] = { trackBCD };
		CdControl(CdlGetTD, param, 1);
		waitForResponse(res, 8);

		/* Result: stat, mm (BCD), ss (BCD) */
		locArray[t].minute = res[1];
		locArray[t].second = res[2];
		locArray[t].sector = 0;
		locArray[t].track = trackBCD;

		printf("CDDA: Track %d at %02X:%02X:00\n", t, res[1], res[2]);
	}

	return lastTrack;
}

/* Set CD mix volume - like Snake's cdSetVol macro */
static void cdSetVol(int vol) {
	/* Set SPU CD audio volume */
	SPU_AVOLL = vol;
	SPU_AVOLR = vol;
}

void initCDDA(void) {
	printf("CDDA: Initializing (Snake-style)...\n");

	/* Set up SPU for CD audio - like Snake's audio_init */
	SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_DAC_ENABLE | SPU_CTRL_I2SA_ENABLE;
	cdDelay();

	/* Set master volume */
	SPU_MVOLL = 0x3FFF;
	SPU_MVOLR = 0x3FFF;

	/* Set CD audio volume */
	cdSetVol(0x7FFF);

	printf("CDDA: SPU configured\n");

	/* Clear any pending interrupts */
	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK | CDROM_HCLRCTL_CLRPRM;
	cdDelay();

	/* Initialize CD-ROM */
	CdControl0(CdlInit);
	waitForResponse(result, 8);
	cdLongDelay();
	waitForResponse(result, 8);  /* Init sends two responses */
	printf("CDDA: CD-ROM initialized\n");

	/* Get TOC - like Snake's CdGetToc(loc) */
	ntoc = CdGetToc(loc);

	/* Set mode for CD-DA with report - exactly like Snake */
	/* param[0] = CdlModeRept|CdlModeDA */
	uint8_t modeParam[1] = { CdlModeRept | CdlModeDA };
	CdControl(CdlSetmode, modeParam, 1);
	waitForResponse(result, 8);
	printf("CDDA: Mode set (CdlModeRept|CdlModeDA = 0x%02X)\n", modeParam[0]);

	/* Demute - like Snake does implicitly */
	CdControl0(CdlDemute);
	waitForResponse(result, 8);
	printf("CDDA: Demuted\n");

	printf("CDDA: Ready! Playing track 2...\n");

	/* Start playing track 2 - exactly like Snake's cdMusicInit */
	/* Snake does: CdControl(CdlPlay, (u_char *)&loc[3], 0); */
	/* The loc structure contains: minute, second, sector, track */
	/* We pass it as parameters to SetLoc then Play */

	/* For track 2 (first audio track), use loc[2] */
	uint8_t setlocParam[3] = { loc[2].minute, loc[2].second, loc[2].sector };
	printf("CDDA: SetLoc to %02X:%02X:%02X (track 2 from TOC)\n",
	       setlocParam[0], setlocParam[1], setlocParam[2]);
	CdControl(CdlSetloc, setlocParam, 3);
	waitForResponse(result, 8);

	/* Play - with no parameter, plays from SetLoc position */
	CdControl0(CdlPlay);
	waitForResponse(result, 8);

	trackToLoop = 2;
	loopMusic = 1;
	currentTrack = 2;

	printf("CDDA: Play command sent!\n");
}

void playCDDATrack(int track) {
	if (track < 1 || track >= ntoc) {
		printf("CDDA: Invalid track %d (have %d tracks)\n", track, ntoc);
		return;
	}

	printf("CDDA: Playing track %d at %02X:%02X:%02X\n",
	       track, loc[track].minute, loc[track].second, loc[track].sector);

	/* SetLoc to track position */
	uint8_t setlocParam[3] = { loc[track].minute, loc[track].second, loc[track].sector };
	CdControl(CdlSetloc, setlocParam, 3);
	waitForResponse(result, 8);

	/* Play */
	CdControl0(CdlPlay);
	waitForResponse(result, 8);

	currentTrack = track;
	trackToLoop = track;
	loopMusic = 1;
}

void stopCDDA(void) {
	printf("CDDA: Stopping\n");
	CdControl0(CdlStop);
	waitForResponse(result, 8);
	loopMusic = 0;
}

void pauseCDDA(void) {
	printf("CDDA: Pausing\n");
	CdControl0(CdlPause);
	waitForResponse(result, 8);
}

bool isCDDAPlaying(void) {
	CdControl0(CdlNop);
	waitForResponse(result, 8);
	return (result[0] & CdlStatPlay) != 0;
}

/* Update function - like Snake's cdMusicUpdate */
void updateCDDA(void) {
	/* Check for CD reports when in report mode */
	/* This would handle looping like Snake does */

	/* For now, simplified - just check if we need to restart */
	if (loopMusic && !isCDDAPlaying()) {
		loopWait++;
		/* Wait a few frames before restarting - Snake waits 5 */
		if (loopWait >= 5) {
			loopWait = 0;
			playCDDATrack(trackToLoop);
		}
	}
}
