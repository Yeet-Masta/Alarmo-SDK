/**
 * Audio implementation for the Nintendo Alarmo.
 * Created in 2024.
 */
#include "audio.h"
#include "main.h"
#include <stm32h7xx_hal.h>
#include <stm32h7xx_hal_sai.h> // Corrected: Using SAI HAL driver instead of LL GPIO
#include <stm32h7xx_hal_mmc.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * SHAA file header structure
 */
typedef struct {
    // General data
    uint8_t     magic[4];           // "SHAA"
    uint32_t    codec;              // 2=DSPADPCM
    uint32_t    header_size;        // Total size of the header in bytes
    uint32_t    reserved;           // Reserved

    // Flags
    uint8_t     format;             // 0=PCM8, 1=PCM16, 2=ADPCM
    uint8_t     type;               // 1=SHAA only, 2=SHAA+SHSA
    uint8_t     channel_count;      // Number of channels
    uint8_t     reserved2;          // Reserved

    // Track information
    uint32_t    sample_rate;        // Sample rate in Hz
    uint32_t    length;             // Length of the audio track in samples
    uint32_t    dsp_offset;         // Offset of the DSP section in bytes
    uint32_t    reserved3;          // Reserved
    uint32_t    loop_start;         // Loop start offset in samples (0 if no loop)
    uint32_t    loop_end;           // Loop end offset in samples (0 if no loop)
} SHAA_Header;

/**
 * DSP context section of SHAA file
 */
typedef struct {
    // Decoder header
    uint32_t    num_samples;        // Number of samples
    uint32_t    num_adpcm_nibbles;  // Number of ADPCM nibbles
    uint32_t    sample_rate;        // Sample rate in Hz

    // Decoder context and addresses
    uint16_t    loop_flag;          // Whether the sample is looped (1=true, 0=false)
    uint16_t    format;             // Sample data format (always 0 for ADPCM)
    uint32_t    sa;                 // ADPCM start loop address (always 2)
    uint32_t    ea;                 // ADPCM end loop address 
    uint32_t    ca;                 // Initial offset value (always 2)
    uint16_t    coef[16];           // Decode coefficients (8 pairs of 16-bit words)

    // Initial decoder state
    uint16_t    gain;               // Always zero for ADPCM
    uint16_t    ps;                 // Predictor/Scale
    uint16_t    yn1;                // Sample history 1
    uint16_t    yn2;                // Sample history 2

    // Loop context
    uint16_t    lps;                // Predictor/Scale for loop context
    uint16_t    lyn1;               // Sample history (n-1) for loop context
    uint16_t    lyn2;               // Sample history (n-2) for loop context
    uint16_t    pad[11];            // Reserved
} SHAA_DSP;

// SAI handle for audio output
SAI_HandleTypeDef hsaiOut;
DMA_HandleTypeDef hdmaSaiTx;

// Audio state
static AUDIO_State audioState = AUDIO_STATE_STOPPED;
static AUDIO_Info audioInfo;
static uint8_t volume = 100;
static bool loopEnabled = false;

// Audio buffers for PCM data
static int16_t audioPcmBuffers[AUDIO_BUFFER_COUNT][AUDIO_BUFFER_SAMPLES];
static uint8_t currentBuffer = 0;
static uint8_t bufferState[AUDIO_BUFFER_COUNT] = {0}; // 0=empty, 1=filling, 2=ready, 3=playing

// ADPCM decoder context
static ADPCM_Context decoderContext;

// File data pointers
static uint8_t* audioData = NULL;
static uint32_t audioDataSize = 0;
static uint32_t audioDataOffset = 0;
static uint32_t currentSample = 0;

// Forward declarations for internal functions
static void AUDIO_SAI_MspInit(SAI_HandleTypeDef* hsai);
static bool AUDIO_ParseSHAAHeader(const uint8_t* data, uint32_t size);
static void AUDIO_FillBuffer(int16_t* buffer, uint32_t sampleCount);
static int16_t AUDIO_DecodeADPCMSample(uint8_t nibble);
static void AUDIO_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai);
static void AUDIO_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai);
static void AUDIO_SAI_ErrorCallback(SAI_HandleTypeDef *hsai);
static void AUDIO_EnableOutput(bool enable);

/**
 * Initialize the audio subsystem
 */
bool AUDIO_Init(void)
{
    // Initialize audio state
    audioState = AUDIO_STATE_STOPPED;
    memset(&audioInfo, 0, sizeof(audioInfo));
    
    // Configure the SAI (Serial Audio Interface)
    hsaiOut.Instance = SAI1_Block_A;
    hsaiOut.Init.AudioMode = SAI_MODEMASTER_TX;
    hsaiOut.Init.Synchro = SAI_ASYNCHRONOUS;
    hsaiOut.Init.OutputDrive = SAI_OUTPUTDRIVE_ENABLE;
    hsaiOut.Init.NoDivider = SAI_MASTERDIVIDER_ENABLE;
    hsaiOut.Init.FIFOThreshold = SAI_FIFOTHRESHOLD_EMPTY;
    hsaiOut.Init.AudioFrequency = SAI_AUDIO_FREQUENCY_44K; // Default, will be adjusted per file
    hsaiOut.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
    hsaiOut.Init.MonoStereoMode = SAI_MONOMODE;
    hsaiOut.Init.CompandingMode = SAI_NOCOMPANDING;
    hsaiOut.Init.TriState = SAI_OUTPUT_NOTRELEASED;
    
    // Configure protocol
    hsaiOut.Init.Protocol = SAI_FREE_PROTOCOL;
    hsaiOut.Init.DataSize = SAI_DATASIZE_16;
    hsaiOut.Init.FirstBit = SAI_FIRSTBIT_MSB;
    hsaiOut.Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
    
    // Configure slots
    hsaiOut.SlotInit.FirstBitOffset = 0;
    hsaiOut.SlotInit.SlotSize = SAI_SLOTSIZE_DATASIZE;
    hsaiOut.SlotInit.SlotNumber = 1;
    hsaiOut.SlotInit.SlotActive = SAI_SLOTACTIVE_0;
    
    // Initialize SAI
    if (HAL_SAI_Init(&hsaiOut) != HAL_OK) {
        return false;
    }
    
    // Register callbacks
    HAL_SAI_RegisterCallback(&hsaiOut, HAL_SAI_TX_COMPLETE_CB_ID, AUDIO_SAI_TxCpltCallback);
    HAL_SAI_RegisterCallback(&hsaiOut, HAL_SAI_TX_HALFCOMPLETE_CB_ID, AUDIO_SAI_TxHalfCpltCallback);
    HAL_SAI_RegisterCallback(&hsaiOut, HAL_SAI_ERROR_CB_ID, AUDIO_SAI_ErrorCallback);
    
    // Initialize buffer states
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        bufferState[i] = 0;
    }
    
    // Disable audio output initially
    AUDIO_EnableOutput(false);
    
    return true;
}

/**
 * MSP initialization for SAI
 */
static void AUDIO_SAI_MspInit(SAI_HandleTypeDef* hsai)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Enable SAI clock
    __HAL_RCC_SAI1_CLK_ENABLE();
    
    // Enable GPIO clocks
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    
    // SAI pins: PE4 (SAI_FS), PE5 (SAI_SCK), PE6 (SAI_SD), PG7 (SAI_MCLK)
    GPIO_InitStruct.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    
    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);
    
    // Initialize Audio Enable pin (PE3)
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
    
    // Set up DMA for SAI TX
    __HAL_RCC_DMA1_CLK_ENABLE();
    
    hdmaSaiTx.Instance = DMA1_Stream0;
    hdmaSaiTx.Init.Request = DMA_REQUEST_SAI1_A;
    hdmaSaiTx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdmaSaiTx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdmaSaiTx.Init.MemInc = DMA_MINC_ENABLE;
    hdmaSaiTx.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdmaSaiTx.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdmaSaiTx.Init.Mode = DMA_CIRCULAR;
    hdmaSaiTx.Init.Priority = DMA_PRIORITY_HIGH;
    hdmaSaiTx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    
    if (HAL_DMA_Init(&hdmaSaiTx) != HAL_OK) {
        //Error_Handler(); ???
    }
    
    // Link DMA to SAI
    __HAL_LINKDMA(hsai, hdmatx, hdmaSaiTx);
    
    // Set up DMA interrupt
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
}

/**
 * Enable or disable audio output
 */
static void AUDIO_EnableOutput(bool enable)
{
    // PE3 is audio enable pin, high = enabled
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, enable ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * Load audio file from storage
 * 
 * @param filename Name of the SHAA file to load
 * @return True if load succeeded
 */
bool AUDIO_Load(const char* filename)
{
    FIL file;               // File object
    FRESULT fr;             // FatFs return code
    UINT bytesRead;         // Bytes read from file
    
    // Stop any currently playing audio
    AUDIO_Stop();
    
    // Free any existing audio data
    if (audioData != NULL) {
        free(audioData);
        audioData = NULL;
        audioDataSize = 0;
    }
    
    // Open the SHAA file (header file)
    fr = f_open(&file, filename, FA_READ);
    if (fr != FR_OK) {
        return false;
    }
    
    // Get file size
    FSIZE_t fileSize = f_size(&file);
    if (fileSize < sizeof(SHAA_Header)) {
        f_close(&file);
        return false;
    }
    
    // Read the header first to determine file type
    uint8_t headerBuffer[256];
    fr = f_read(&file, headerBuffer, sizeof(headerBuffer), &bytesRead);
    if (fr != FR_OK || bytesRead < sizeof(SHAA_Header)) {
        f_close(&file);
        return false;
    }
    
    // Parse header to get audio format info
    if (!AUDIO_ParseSHAAHeader(headerBuffer, bytesRead)) {
        f_close(&file);
        return false;
    }
    
    // Check if this is a split file format (SHAA+SHSA)
    bool splitFile = ((SHAA_Header*)headerBuffer)->type == AUDIO_TYPE_SHAA_SHSA;
    FIL dataFile;
    FSIZE_t dataFileSize;
    
    if (splitFile) {
        // Create SHSA filename by replacing extension
        char dataFilename[256];
        strcpy(dataFilename, filename);
        
        // Find the extension and replace it
        char* ext = strrchr(dataFilename, '.');
        if (ext != NULL) {
            strcpy(ext, ".SHSA");
        } else {
            // If no extension found, just append .SHSA
            strcat(dataFilename, ".SHSA");
        }
        
        // Close header file and open data file
        f_close(&file);
        
        // Open the SHSA data file
        fr = f_open(&dataFile, dataFilename, FA_READ);
        if (fr != FR_OK) {
            return false;
        }
        
        // Get data file size
        dataFileSize = f_size(&dataFile);
        
        // Use the data file for subsequent operations
        file = dataFile;
    } else {
        // For single file format, just rewind and use the whole file
        dataFileSize = fileSize;
        f_lseek(&file, 0);
    }
    
    // Allocate memory for audio data - ensure it's aligned for DMA transfers
    // Using aligned allocation if possible, otherwise fallback to regular malloc
    // and hope the memory is properly aligned
    audioData = (uint8_t*)malloc(dataFileSize);
    if (audioData == NULL) {
        f_close(&file);
        return false;
    }
    audioDataSize = dataFileSize;
    
    // For large files, use direct MMC access for better performance
    // We'll check if file size is above a certain threshold
    #define DIRECT_MMC_THRESHOLD (64 * 1024) // 64KB threshold
    
    if (dataFileSize >= DIRECT_MMC_THRESHOLD && 
        // Only use direct access for files that are properly aligned to sectors
        (dataFileSize % 512) == 0) {
        
        // Get physical sector information from FatFs
        // Get information about the file's location on disk
        DWORD clst, sect;
        UINT sectCount;
        
        // Get first cluster of the file
        clst = file.obj.sclust;
        if (clst == 0) {
            // Invalid cluster, use standard f_read instead
            goto use_standard_read;
        }
        
        // Convert cluster to sector
        sect = ((file.obj.fs)->database) + (((clst) - 2) * ((file.obj.fs)->csize));
        
        // Calculate number of sectors to read
        sectCount = (dataFileSize + 511) / 512; // Round up to nearest sector
        
        // Read data directly using HAL_MMC_ReadBlocks
        if (HAL_MMC_ReadBlocks(&MMCHandle, (uint8_t*)audioData, sect, sectCount, 1000) != HAL_OK) {
            // If direct read fails, fall back to standard read
            goto use_standard_read;
        }
        
        // Successfully read data using direct MMC access
    } else {
use_standard_read:
        // For smaller files or if direct access fails, use standard FatFs read
        f_lseek(&file, 0);
        
        // Read the entire file in one operation
        fr = f_read(&file, audioData, dataFileSize, &bytesRead);
        if (fr != FR_OK || bytesRead != dataFileSize) {
            free(audioData);
            audioData = NULL;
            f_close(&file);
            return false;
        }
    }
    
    // Close the file
    f_close(&file);
    
    // Set appropriate offset based on file type
    if (!splitFile) {
        // In single file format, data is after the header
        SHAA_Header* header = (SHAA_Header*)audioData;
        audioDataOffset = header->header_size;
    } else {
        // In split file format, data starts at beginning of SHSA file
        audioDataOffset = 0;
    }
    
    // Reset playback state
    currentSample = 0;
    
    return true;
}

/**
 * Parse SHAA header data
 */
static bool AUDIO_ParseSHAAHeader(const uint8_t* data, uint32_t size)
{
    if (size < sizeof(SHAA_Header)) {
        return false;
    }
    
    SHAA_Header* header = (SHAA_Header*)data;
    
    // Verify magic "SHAA"
    if (header->magic[0] != 'S' || header->magic[1] != 'H' || 
        header->magic[2] != 'A' || header->magic[3] != 'A') {
        return false;
    }
    
    // Check codec type (must be DSPADPCM)
    if (header->codec != 2) {
        return false;
    }
    
    // Store file info
    audioInfo.format = header->format;
    audioInfo.sampleRate = header->sample_rate;
    audioInfo.length = header->length;
    audioInfo.loopStart = header->loop_start;
    audioInfo.loopEnd = header->loop_end;
    audioInfo.hasLoop = (header->loop_start != 0 || header->loop_end != 0);
    
    // If we have a DSP section, parse it
    if (header->dsp_offset > 0 && header->dsp_offset + sizeof(SHAA_DSP) <= size) {
        SHAA_DSP* dsp = (SHAA_DSP*)(data + header->dsp_offset);
        
        // Copy ADPCM coefficients
        for (int i = 0; i < 16; i++) {
            audioInfo.adpcm.coeffs[i] = dsp->coef[i];
        }
        
        // Copy initial state
        audioInfo.adpcm.ps = dsp->ps;
        audioInfo.adpcm.yn1 = dsp->yn1;
        audioInfo.adpcm.yn2 = dsp->yn2;
    }
    
    // Update SAI audio frequency based on the sample rate
    uint32_t saiFreq;
    if (header->sample_rate == 48000) {
        saiFreq = SAI_AUDIO_FREQUENCY_48K;
    } else if (header->sample_rate == 44100) {
        saiFreq = SAI_AUDIO_FREQUENCY_44K;
    } else if (header->sample_rate == 32000) {
        saiFreq = SAI_AUDIO_FREQUENCY_32K;
    } else if (header->sample_rate == 22050) {
        saiFreq = SAI_AUDIO_FREQUENCY_22K;
    } else if (header->sample_rate == 16000) {
        saiFreq = SAI_AUDIO_FREQUENCY_16K;
    } else if (header->sample_rate == 11025) {
        saiFreq = SAI_AUDIO_FREQUENCY_11K;
    } else if (header->sample_rate == 8000) {
        saiFreq = SAI_AUDIO_FREQUENCY_8K;
    } else {
        // Default to 44.1kHz if unsupported
        saiFreq = SAI_AUDIO_FREQUENCY_44K;
    }
    
    // Update SAI configuration
    HAL_SAI_DeInit(&hsaiOut);
    hsaiOut.Init.AudioFrequency = saiFreq;
    
    if (HAL_SAI_Init(&hsaiOut) != HAL_OK) {
        return false;
    }
    
    return true;
}

/**
 * Start audio playback
 */
bool AUDIO_Play(bool loop)
{
    // Check if audio data is loaded
    if (audioData == NULL || audioDataSize == 0) {
        return false;
    }
    
    // If already playing, do nothing
    if (audioState == AUDIO_STATE_PLAYING) {
        loopEnabled = loop;
        return true;
    }
    
    // Initialize variables
    loopEnabled = loop;
    currentSample = 0;
    audioDataOffset = 0;
    currentBuffer = 0;
    
    // Initialize ADPCM context
    decoderContext = audioInfo.adpcm;
    
    // Clear buffer states
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        bufferState[i] = 0;
    }
    
    // Fill first two buffers
    AUDIO_FillBuffer(audioPcmBuffers[0], AUDIO_BUFFER_SAMPLES);
    bufferState[0] = 2; // Ready
    
    AUDIO_FillBuffer(audioPcmBuffers[1], AUDIO_BUFFER_SAMPLES);
    bufferState[1] = 2; // Ready
    
    // Enable audio output
    AUDIO_EnableOutput(true);
    
    // Start playback with DMA
    if (HAL_SAI_Transmit_DMA(&hsaiOut, (uint8_t*)audioPcmBuffers[0], 
                              AUDIO_BUFFER_SAMPLES * sizeof(int16_t) / sizeof(uint8_t)) != HAL_OK) {
        AUDIO_EnableOutput(false);
        return false;
    }
    
    // Update state
    audioState = AUDIO_STATE_PLAYING;
    bufferState[0] = 3; // Playing
    currentBuffer = 0;
    
    return true;
}

/**
 * Pause audio playback
 */
void AUDIO_Pause(void)
{
    if (audioState == AUDIO_STATE_PLAYING) {
        // Stop SAI DMA transfer
        HAL_SAI_DMAStop(&hsaiOut);
        
        // Update state
        audioState = AUDIO_STATE_PAUSED;
        
        // Disable audio output
        AUDIO_EnableOutput(false);
    }
}

/**
 * Resume audio playback
 */
void AUDIO_Resume(void)
{
    if (audioState == AUDIO_STATE_PAUSED) {
        // Enable audio output
        AUDIO_EnableOutput(true);
        
        // Restart DMA transfer
        if (HAL_SAI_Transmit_DMA(&hsaiOut, (uint8_t*)audioPcmBuffers[currentBuffer], 
                                  AUDIO_BUFFER_SAMPLES * sizeof(int16_t) / sizeof(uint8_t)) == HAL_OK) {
            // Update state
            audioState = AUDIO_STATE_PLAYING;
            bufferState[currentBuffer] = 3; // Playing
        }
    }
}

/**
 * Stop audio playback
 */
void AUDIO_Stop(void)
{
    if (audioState != AUDIO_STATE_STOPPED) {
        // Stop SAI DMA transfer
        HAL_SAI_DMAStop(&hsaiOut);
        
        // Update state
        audioState = AUDIO_STATE_STOPPED;
        
        // Reset variables
        currentSample = 0;
        audioDataOffset = 0;
        
        // Reset buffer states
        for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
            bufferState[i] = 0;
        }
        
        // Disable audio output
        AUDIO_EnableOutput(false);
    }
}

/**
 * Set audio volume
 */
void AUDIO_SetVolume(uint8_t vol)
{
    // Clamp volume to 0-100
    volume = (vol > 100) ? 100 : vol;
}

/**
 * Get current playback position
 */
uint32_t AUDIO_GetPosition(void)
{
    return currentSample;
}

/**
 * Get current audio state
 */
AUDIO_State AUDIO_GetState(void)
{
    return audioState;
}

/**
 * Get audio file information
 */
const AUDIO_Info* AUDIO_GetInfo(void)
{
    return &audioInfo;
}

/**
 * Fill PCM buffer with decoded ADPCM data
 */
static void AUDIO_FillBuffer(int16_t* buffer, uint32_t sampleCount)
{
    // Check if we've reached the end of the file
    if (currentSample >= audioInfo.length) {
        if (loopEnabled && audioInfo.hasLoop) {
            // Jump to loop start
            currentSample = audioInfo.loopStart;
            
            // Reset ADPCM context to loop context
            // In a real implementation, you'd need to store the loop context properly
            decoderContext = audioInfo.adpcm;
        } else {
            // End of file, fill with silence
            memset(buffer, 0, sampleCount * sizeof(int16_t));
            return;
        }
    }
    
    // Fill buffer with decoded samples
    for (uint32_t i = 0; i < sampleCount; i++) {
        if (currentSample < audioInfo.length) {
            // Calculate ADPCM byte and nibble position
            uint32_t bytePos = currentSample / 2;
            bool highNibble = (currentSample % 2 == 0);
            
            // Extract nibble (4 bits)
            uint8_t adpcmData = audioData[audioDataOffset + bytePos];
            uint8_t nibble;
            
            if (highNibble) {
                nibble = (adpcmData >> 4) & 0xF;
            } else {
                nibble = adpcmData & 0xF;
            }
            
            // Decode ADPCM sample
            int16_t sample = AUDIO_DecodeADPCMSample(nibble);
            
            // Apply volume
            if (volume < 100) {
                sample = (sample * volume) / 100;
            }
            
            // Store decoded sample
            buffer[i] = sample;
            
            // Update position
            currentSample++;
        } else {
            // End of file reached during decoding
            if (loopEnabled && audioInfo.hasLoop) {
                // Jump to loop start
                currentSample = audioInfo.loopStart;
                
                // Reset ADPCM context to loop context
                decoderContext = audioInfo.adpcm;
            } else {
                // Fill remaining buffer with silence
                memset(&buffer[i], 0, (sampleCount - i) * sizeof(int16_t));
                break;
            }
        }
    }
}

/**
 * Decode a single ADPCM sample
 */
static int16_t AUDIO_DecodeADPCMSample(uint8_t nibble)
{
    // Extract ADPCM scale and predictor values
    uint8_t scale = decoderContext.ps & 0xF;
    uint8_t predictor = (decoderContext.ps >> 4) & 0x7;
    
    // Convert nibble to signed value (-8 to +7)
    int8_t sample = nibble;
    if (sample > 7) {
        sample -= 16;
    }
    
    // Calculate prediction
    int32_t prediction = 0;
    prediction += (decoderContext.yn1 * decoderContext.coeffs[predictor*2+0]) >> 11;
    prediction += (decoderContext.yn2 * decoderContext.coeffs[predictor*2+1]) >> 11;
    
    // Add delta
    int32_t delta = sample << scale;
    int32_t newSample = prediction + delta;
    
    // Clamp to int16_t range
    if (newSample > 32767) {
        newSample = 32767;
    } else if (newSample < -32768) {
        newSample = -32768;
    }
    
    // Update sample history
    decoderContext.yn2 = decoderContext.yn1;
    decoderContext.yn1 = (int16_t)newSample;
    
    return (int16_t)newSample;
}

/**
 * Process audio (should be called regularly in the main loop)
 */
void AUDIO_Process(void)
{
    // Only process if we're playing
    if (audioState != AUDIO_STATE_PLAYING) {
        return;
    }
    
    // Check if any buffer needs filling
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        if (bufferState[i] == 0) { // Empty buffer
            // Mark as filling
            bufferState[i] = 1;
            
            // Fill buffer
            AUDIO_FillBuffer(audioPcmBuffers[i], AUDIO_BUFFER_SAMPLES);
            
            // Mark as ready
            bufferState[i] = 2;
        }
    }
}

/**
 * SAI TX complete callback
 */
static void AUDIO_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    // Get next buffer to play
    uint8_t nextBuffer = (currentBuffer + 1) % AUDIO_BUFFER_COUNT;
    
    // Mark current buffer as empty
    bufferState[currentBuffer] = 0;
    
    // Check if next buffer is ready
    if (bufferState[nextBuffer] == 2) {
        // Mark as playing
        bufferState[nextBuffer] = 3;
        currentBuffer = nextBuffer;
    } else {
        // No buffer ready, stop playback
        AUDIO_Stop();
    }
}

/**
 * SAI TX half complete callback
 */
static void AUDIO_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    // Optional: Handle buffer streaming if needed
}

/**
 * SAI error callback
 */
static void AUDIO_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    // Handle error (e.g., stop playback)
    AUDIO_Stop();
}

/**
 * DMA interrupt handler
 */
void DMA1_Stream0_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdmaSaiTx);
}