/*
 * XA Audio streaming for PS1 bare-metal
 * Streams XA-ADPCM audio from CD-ROM
 * Based on PSn00bSDK cdxa example
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize XA audio streaming subsystem */
void xa_init(void);

/* Start playing XA audio file
 * filename: ISO9660 path like "\\MUSIC.XA;1"
 * channel: XA channel (0-7) for interleaved streams
 * loop: whether to loop when reaching end
 */
void xa_play(const char *filename, int channel, bool loop);

/* Start playing XA audio from LBA position directly */
void xa_play_lba(uint32_t startLBA, int channel, bool loop);

/* Stop XA playback */
void xa_stop(void);

/* Set XA playback volume (0-127) */
void xa_set_volume(int vol);

/* Check if XA is currently playing */
bool xa_is_playing(void);

/* Update XA state - call periodically for looping support */
void xa_update(void);

#ifdef __cplusplus
}
#endif
