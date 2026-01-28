/*
 * SPU helper functions for PS1 bare-metal
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the SPU hardware */
void setupSPU(void);

/* Unmute SPU - call after upload is complete */
void spuUnmute(void);

/* Upload VAG audio data to SPU RAM and return the SPU address */
uint32_t uploadVAG(const void *data, size_t size);

/* Play a sample on a specific channel (0-23) */
void playSample(int channel, uint32_t spuAddr, int sampleRate, int volume);

/* Stop playback on a channel */
void stopChannel(int channel);

#ifdef __cplusplus
}
#endif
