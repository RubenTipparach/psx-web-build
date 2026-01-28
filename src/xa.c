/*
 * XA Audio streaming for PS1 bare-metal
 * Streams XA-ADPCM audio from CD-ROM
 * Based on PSn00bSDK cdxa example
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "xa.h"
#include "ps1/registers.h"

/* CD-ROM commands */
#define CD_CMD_GETSTAT   0x01
#define CD_CMD_SETLOC    0x02
#define CD_CMD_PLAY      0x03
#define CD_CMD_STOP      0x08
#define CD_CMD_PAUSE     0x09
#define CD_CMD_INIT      0x0A
#define CD_CMD_DEMUTE    0x0C
#define CD_CMD_SETFILTER 0x0D
#define CD_CMD_SETMODE   0x0E
#define CD_CMD_GETLOCP   0x11
#define CD_CMD_READ_S    0x1B  /* Real-time streaming read */

/* Mode flags for XA playback */
#define CD_MODE_SPEED_2X   0x80  /* Double speed (required for 37800 Hz XA) */
#define CD_MODE_XA_ADPCM   0x40  /* Enable XA-ADPCM decoding */
#define CD_MODE_XA_FILTER  0x08  /* Enable file/channel filtering */

/* XA state */
static bool xa_playing = false;
static bool xa_looping = false;
static uint32_t xa_start_lba = 0;
static int xa_channel = 0;

/* Small delay for CD-ROM operations */
static void cdDelay(void) {
	for (volatile int i = 0; i < 10000; i++)
		__asm__ volatile("");
}

/* Wait for CD-ROM controller to be ready */
static void waitCDReady(void) {
	while (CDROM_HSTS & CDROM_HSTS_BUSYSTS)
		__asm__ volatile("");
}

/* Wait for CD-ROM response */
static int waitForResponse(void) {
	int timeout = 100000;

	CDROM_ADDRESS = 1;
	while (!(CDROM_HINTSTS & CDROM_HINT_INT_BITMASK) && timeout > 0) {
		timeout--;
		__asm__ volatile("");
	}

	int intType = CDROM_HINTSTS & CDROM_HINT_INT_BITMASK;

	/* Discard response bytes */
	CDROM_ADDRESS = 1;
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
static void sendCommand(uint8_t cmd) {
	waitCDReady();
	CDROM_ADDRESS = 0;
	CDROM_COMMAND = cmd;
	cdDelay();
}

/* Send command with one parameter */
static void sendCommand1(uint8_t cmd, uint8_t p1) {
	waitCDReady();
	CDROM_ADDRESS = 0;
	CDROM_PARAMETER = p1;
	CDROM_COMMAND = cmd;
	cdDelay();
}

/* Send command with two parameters */
static void sendCommand2(uint8_t cmd, uint8_t p1, uint8_t p2) {
	waitCDReady();
	CDROM_ADDRESS = 0;
	CDROM_PARAMETER = p1;
	CDROM_PARAMETER = p2;
	CDROM_COMMAND = cmd;
	cdDelay();
}

/* Send command with three parameters */
static void sendCommand3(uint8_t cmd, uint8_t p1, uint8_t p2, uint8_t p3) {
	waitCDReady();
	CDROM_ADDRESS = 0;
	CDROM_PARAMETER = p1;
	CDROM_PARAMETER = p2;
	CDROM_PARAMETER = p3;
	CDROM_COMMAND = cmd;
	cdDelay();
}

/* Convert LBA to BCD MSF format */
static void lba_to_msf(uint32_t lba, uint8_t *min, uint8_t *sec, uint8_t *frame) {
	/* Add 150 sectors (2 seconds) for lead-in */
	lba += 150;

	uint32_t m = lba / (75 * 60);
	uint32_t s = (lba / 75) % 60;
	uint32_t f = lba % 75;

	/* Convert to BCD */
	*min = ((m / 10) << 4) | (m % 10);
	*sec = ((s / 10) << 4) | (s % 10);
	*frame = ((f / 10) << 4) | (f % 10);
}

void xa_init(void) {
	printf("XA: Initializing...\n");

	/* Enable SPU with CD audio input */
	SPU_CTRL = SPU_CTRL_ENABLE | SPU_CTRL_DAC_ENABLE | SPU_CTRL_I2SA_ENABLE;
	cdDelay();

	/* Set master volume */
	SPU_MVOLL = 0x3FFF;
	SPU_MVOLR = 0x3FFF;

	/* Set CD audio volume to max */
	SPU_AVOLL = 0x7FFF;
	SPU_AVOLR = 0x7FFF;

	printf("XA: SPU configured\n");

	/* Clear any pending interrupts */
	CDROM_ADDRESS = 1;
	CDROM_HCLRCTL = CDROM_HCLRCTL_CLRINT_BITMASK | CDROM_HCLRCTL_CLRPRM;
	cdDelay();

	/* Initialize CD-ROM */
	sendCommand(CD_CMD_INIT);
	waitForResponse();
	for (volatile int i = 0; i < 100000; i++) __asm__ volatile("");
	waitForResponse();  /* Init sends two responses */
	printf("XA: CD-ROM initialized\n");

	/* Demute CD audio */
	sendCommand(CD_CMD_DEMUTE);
	waitForResponse();
	printf("XA: Demuted\n");
}

void xa_play(const char *filename, int channel, bool loop) {
	/* For now, use hardcoded LBA - finding files on ISO9660 is complex
	 * The XA file will be placed at a known location by mkpsxiso
	 * We'll need to determine the LBA from the disc layout
	 */
	printf("XA: Play requested: %s channel=%d loop=%d\n", filename, channel, loop);

	/* Hardcode LBA for MUSIC.XA - adjust based on disc layout
	 * After data track + padding, typically around sector 23+ */
	uint32_t lba = 23;  /* Will be updated based on actual disc layout */

	xa_play_lba(lba, channel, loop);
}

void xa_play_lba(uint32_t startLBA, int channel, bool loop) {
	printf("XA: Starting from LBA %lu, channel=%d, loop=%d\n",
	       (unsigned long)startLBA, channel, loop);

	xa_start_lba = startLBA;
	xa_channel = channel;
	xa_looping = loop;

	/* Stop any current playback */
	sendCommand(CD_CMD_PAUSE);
	waitForResponse();

	/* Set mode for XA-ADPCM playback:
	 * - CD_MODE_SPEED_2X: Double speed for 37800 Hz
	 * - CD_MODE_XA_ADPCM: Enable XA audio decoding
	 * - CD_MODE_XA_FILTER: Enable file/channel filtering
	 */
	uint8_t mode = CD_MODE_SPEED_2X | CD_MODE_XA_ADPCM | CD_MODE_XA_FILTER;
	sendCommand1(CD_CMD_SETMODE, mode);
	waitForResponse();
	printf("XA: Mode=0x%02X\n", mode);

	/* Set XA filter: file=0, channel as specified
	 * File number 0 matches psxavenc -F 0 parameter (default)
	 */
	sendCommand2(CD_CMD_SETFILTER, 0, channel);
	waitForResponse();
	printf("XA: Filter file=0 channel=%d\n", channel);

	/* Set location (MSF in BCD) */
	uint8_t min, sec, frame;
	lba_to_msf(startLBA, &min, &sec, &frame);
	sendCommand3(CD_CMD_SETLOC, min, sec, frame);
	waitForResponse();
	printf("XA: SetLoc %02X:%02X:%02X\n", min, sec, frame);

	/* Start real-time streaming read */
	sendCommand(CD_CMD_READ_S);
	waitForResponse();

	xa_playing = true;
	printf("XA: Streaming started!\n");
}

void xa_stop(void) {
	printf("XA: Stopping\n");
	sendCommand(CD_CMD_PAUSE);
	waitForResponse();
	xa_playing = false;
}

void xa_set_volume(int vol) {
	/* Scale 0-127 to 0-0x7FFF */
	uint16_t scaled = (vol * 0x7FFF) / 127;
	SPU_AVOLL = scaled;
	SPU_AVOLR = scaled;
}

bool xa_is_playing(void) {
	return xa_playing;
}

void xa_update(void) {
	/* Check for end of stream and loop if needed */
	/* This would require monitoring CD-ROM interrupts/status
	 * For now, this is a placeholder - looping needs proper
	 * end-of-file detection via CD-ROM callbacks */
	if (xa_playing && xa_looping) {
		/* TODO: Detect end of XA stream and restart */
	}
}
