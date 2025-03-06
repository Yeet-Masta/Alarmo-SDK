/**
 * Audio implementation for the Nintendo Alarmo.
 * Created in 2024.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ff.h"
#include "diskio.h"

/**
 * Maximum number of samples to buffer at once
 */
#define AUDIO_BUFFER_SAMPLES 1024

/**
 * Number of buffers to use for double-buffering
 */
#define AUDIO_BUFFER_COUNT 2

/**
 * ADPCM Frame size in samples
 */
#define ADPCM_FRAME_SAMPLES 14

/**
 * Audio data formats supported
 */
typedef enum {
    AUDIO_FORMAT_ADPCM = 2,
} AUDIO_Format;

/**
 * ADPCM coefficient context for decoding
 */
typedef struct {
    int16_t coeffs[16];
    int16_t yn1;
    int16_t yn2;
    uint16_t ps;
} ADPCM_Context;

/**
 * Audio file header information
 */
typedef struct {
    uint32_t format;             // Audio format (ADPCM, etc.)
    uint32_t sampleRate;         // Sample rate in Hz
    uint32_t length;             // Total length in samples
    uint32_t loopStart;          // Loop start in samples (0 if no loop)
    uint32_t loopEnd;            // Loop end in samples (0 if no loop)
    bool hasLoop;                // Whether the file has loop points
    ADPCM_Context adpcm;         // ADPCM context if used
} AUDIO_Info;

/**
 * Audio playback state
 */
typedef enum {
    AUDIO_STATE_STOPPED,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED
} AUDIO_State;

/**
 * Audio file types
 */
typedef enum {
    AUDIO_TYPE_SHAA_ONLY = 1,    // Single file format
    AUDIO_TYPE_SHAA_SHSA = 2     // Split header/data format
} AUDIO_Type;

/**
 * Initialize audio subsystem
 * 
 * @return True if initialization succeeded
 */
bool AUDIO_Init(void);

/**
 * Load audio file
 * 
 * @param filename Name of the SHAA file to load
 * @return True if load succeeded
 */
bool AUDIO_Load(const char* filename);

/**
 * Start audio playback
 * 
 * @param loop Whether to loop the audio when it reaches the end
 * @return True if playback started
 */
bool AUDIO_Play(bool loop);

/**
 * Pause audio playback
 */
void AUDIO_Pause(void);

/**
 * Resume audio playback from paused state
 */
void AUDIO_Resume(void);

/**
 * Stop audio playback
 */
void AUDIO_Stop(void);

/**
 * Set audio volume
 * 
 * @param volume Volume level (0-100)
 */
void AUDIO_SetVolume(uint8_t volume);

/**
 * Get current playback position in samples
 * 
 * @return Current position
 */
uint32_t AUDIO_GetPosition(void);

/**
 * Get the current playback state
 * 
 * @return Current state (playing, paused, stopped)
 */
AUDIO_State AUDIO_GetState(void);

/**
 * Get information about currently loaded audio
 * 
 * @return Audio info structure
 */
const AUDIO_Info* AUDIO_GetInfo(void);

/**
 * Process audio (should be called regularly in the main loop)
 */
void AUDIO_Process(void);
