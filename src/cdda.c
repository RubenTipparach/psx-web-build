/*
 * CD-DA Audio Playback for PS1 bare-metal
 * Plays audio tracks directly from the disc
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "cdda.h"

/* CD-ROM registers */
#define CDROM_REG0 (*(volatile uint8_t *)0x1F801800)  /* Index/Status */
#define CDROM_REG1 (*(volatile uint8_t *)0x1F801801)  /* Command/Response */
#define CDROM_REG2 (*(volatile uint8_t *)0x1F801802)  /* Parameter/Data */
#define CDROM_REG3 (*(volatile uint8_t *)0x1F801803)  /* Request/IRQ */

/* SPU registers for CD audio volume */
#define SPU_CD_VOL_L   (*(volatile uint16_t *)0x1F801DB0)  /* CD audio volume left */
#define SPU_CD_VOL_R   (*(volatile uint16_t *)0x1F801DB2)  /* CD audio volume right */
#define SPU_MAIN_VOL_L (*(volatile uint16_t *)0x1F801D80)  /* Main volume left */
#define SPU_MAIN_VOL_R (*(volatile uint16_t *)0x1F801D82)  /* Main volume right */
#define SPU_CTRL       (*(volatile uint16_t *)0x1F801DAA)  /* SPU control */
#define SPU_STAT       (*(volatile uint16_t *)0x1F801DAE)  /* SPU status */

/* CD-ROM commands */
#define CD_CMD_GETSTAT   0x01  /* Get status */
#define CD_CMD_SETLOC    0x02  /* Set location (MSF) */
#define CD_CMD_PLAY      0x03  /* Play CD-DA */
#define CD_CMD_STOP      0x08  /* Stop */
#define CD_CMD_PAUSE     0x09  /* Pause */
#define CD_CMD_INIT      0x0A  /* Initialize */
#define CD_CMD_DEMUTE    0x0C  /* Demute (enable CD audio output) */
#define CD_CMD_SETMODE   0x0E  /* Set mode */

/* Status bits */
#define CDROM_STAT_BUSY  (1 << 7)  /* Command busy */

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

/* Wait for CD-ROM to be ready */
static void waitCDReady(void) {
    int timeout = 100000;
    while ((CDROM_REG0 & CDROM_STAT_BUSY) && timeout > 0) {
        timeout--;
        __asm__ volatile("");
    }
}

/* Wait for response with timeout */
static int waitForResponse(void) {
    int timeout = 100000;

    /* Wait for interrupt flag (index 1, reg3 bit 0-2) */
    CDROM_REG0 = 1;
    while (!(CDROM_REG3 & 0x07) && timeout > 0) {
        timeout--;
        __asm__ volatile("");
    }

    if (timeout == 0) {
        return -1;
    }

    /* Read interrupt type */
    int intType = CDROM_REG3 & 0x07;

    /* Acknowledge interrupt */
    CDROM_REG3 = 0x07;
    cdDelay();

    /* Clear parameter FIFO */
    CDROM_REG0 = 0;
    CDROM_REG3 = 0x40;

    return intType;
}

void initCDDA(void) {
    printf("CDDA: Initializing...\n");

    /* Enable SPU and set CD audio volume */
    SPU_CTRL = 0x8000;  /* SPU enable, unmute */
    cdDelay();
    SPU_MAIN_VOL_L = 0x3FFF;  /* Max main volume */
    SPU_MAIN_VOL_R = 0x3FFF;
    SPU_CD_VOL_L = 0x7FFF;    /* Max CD volume */
    SPU_CD_VOL_R = 0x7FFF;
    printf("CDDA: SPU volumes set\n");

    /* Reset controller state */
    CDROM_REG0 = 1;
    CDROM_REG3 = 0x07;  /* Acknowledge any pending interrupts */
    cdDelay();

    CDROM_REG0 = 0;
    CDROM_REG3 = 0x40;  /* Clear parameter FIFO */
    cdDelay();

    /* Send Init command */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_INIT;
    cdLongDelay();
    waitForResponse();
    cdLongDelay();
    waitForResponse();  /* Init sends two responses */

    printf("CDDA: Init done\n");

    /* Get initial status */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_GETSTAT;
    cdDelay();
    waitForResponse();

    /* Set mode: bit 0 = CDDA, bit 7 = speed (0 = normal) */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG2 = 0x00;  /* Normal mode */
    CDROM_REG1 = CD_CMD_SETMODE;
    cdDelay();
    waitForResponse();

    printf("CDDA: Mode set\n");

    /* Demute CD audio output */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_DEMUTE;
    cdDelay();
    waitForResponse();

    printf("CDDA: Demuted\n");
}

void playCDDATrack(int track) {
    (void)track;  /* We'll use explicit MSF position instead */
    printf("CDDA: Playing audio track\n");

    /* Stop any current playback first */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_STOP;
    cdDelay();
    waitForResponse();
    cdLongDelay();

    /* Clear parameter FIFO */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG3 = 0x40;
    cdDelay();

    /* SetLoc to audio track start position */
    /* From cue file: Track 2 INDEX 01 is at 00:02:43 */
    /* Parameters are in BCD format: MM, SS, FF */
    /* BCD: 2 min = 0x02, 43 sec = 0x43, 0 frames = 0x00 */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG2 = 0x02;  /* Minutes: 02 in BCD */
    CDROM_REG2 = 0x45;  /* Seconds: 45 in BCD (slightly after 02:43) */
    CDROM_REG2 = 0x00;  /* Frames: 00 in BCD */
    CDROM_REG1 = CD_CMD_SETLOC;
    cdDelay();

    int resp = waitForResponse();
    printf("CDDA: SetLoc response: %d\n", resp);

    /* Now Play from the set location (no parameter = play from SetLoc position) */
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_PLAY;
    cdDelay();

    resp = waitForResponse();
    printf("CDDA: Play response: %d\n", resp);
}

void stopCDDA(void) {
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_STOP;
    cdDelay();
    waitForResponse();
}

bool isCDDAPlaying(void) {
    waitCDReady();
    CDROM_REG0 = 0;
    CDROM_REG1 = CD_CMD_GETSTAT;
    cdDelay();
    waitForResponse();

    CDROM_REG0 = 1;
    uint8_t stat = CDROM_REG1;

    /* Bit 7 = playing */
    return (stat & 0x80) != 0;
}
