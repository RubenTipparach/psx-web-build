/*
 * CD-DA Audio Playback for PS1 bare-metal
 * Plays audio tracks directly from the disc
 */

#ifndef CDDA_H
#define CDDA_H

#include <stdint.h>
#include <stdbool.h>

/* Initialize the CD-ROM subsystem */
void initCDDA(void);

/* Play CD-DA audio track (track 2 is first audio track after data) */
void playCDDATrack(int track);

/* Stop CD-DA playback */
void stopCDDA(void);

/* Pause CD-DA playback */
void pauseCDDA(void);

/* Check if CD-DA is currently playing */
bool isCDDAPlaying(void);

/* Call periodically to handle looping */
void updateCDDA(void);

#endif /* CDDA_H */
