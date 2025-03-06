#include "main.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stm32h7xx_hal.h>
#include <stm32h7xx_ll_rcc.h>
#include "system_stm32h7xx.h"
//#include "stm32h7xx_hal_mmc.h"
#include "libalarmo/lcd.h"
#include "libalarmo/input.h"
#include "libalarmo/audio.h"
#include "qrcodegen.h"
#include <math.h>
#include "text_render.h"

#include "cat_png.h"
#include "dog_png.h"

// Menu system definitions
#define MAX_MENU_ITEMS 10
#define MENU_ITEM_HEIGHT 40
#define MENU_ITEM_PADDING 10
#define MENU_TITLE_HEIGHT 50

// Double buffering system
#define FRAME_RATE_MS 33  // ~30 FPS

// Memory alignment for LCD buffers (fixes vertical lines)
#define BUFFER_ALIGNMENT 32

#define VISIBLE_MENU_ITEMS 4  // Maximum number of menu items visible at once
#define SCROLL_INDICATOR_HEIGHT 5  // Height of scroll indicators

#define DIAL_THRESHOLD 10.0f        // Minimum angle change to register a dial movement
#define DIAL_DEBOUNCE_TIME 150      // Time in ms to wait before accepting another dial input
#define DIAL_DIRECTION_MARGIN 45.0f // Prevent unintended direction changes when crossing 0/360

// File browser definitions
#define MAX_FILES_PER_PAGE 6
#define MAX_FILENAME_LEN 32
#define MAX_PATH_LEN 256

// AES data structure
typedef struct {
    uint32_t iv[4];
    uint32_t encrypted_parts[4][4];
} aes_data_t;

typedef enum {
    APP_CAT_VIEWER,
    APP_QR_CODE,
    APP_COLOR_WHEEL,
    APP_SETTINGS,
    APP_CAT_PNG,     // New app for cat PNG
    APP_QR_DISPLAY,  // New app for QR code
    APP_BUTTER_PNG,
    APP_FILE_BROWSER,
    APP_MMC_READ_TEST,
    APP_MMC_KIOXIA_TEST,
    // Add more apps here
    APP_COUNT
} AppType;

typedef struct {
    const char* title;
    AppType type;
    void (*handler)(uint8_t* buffer);  // Modified to accept buffer parameter
    uint8_t r, g, b;  // Icon color
} MenuItem;

typedef struct {
    MenuItem items[MAX_MENU_ITEMS];
    int itemCount;
    int selectedIndex;
    int scrollOffset;  // New field: Current scroll position
    const char* title;
    bool active;
} Menu;

typedef struct {
    char filename[MAX_FILENAME_LEN];
    bool isDirectory;
} FileEntry;

typedef struct {
    char currentPath[MAX_PATH_LEN];
    FileEntry files[MAX_FILES_PER_PAGE * 2]; // Double buffer size to accommodate more files
    int fileCount;
    int selectedIndex;
    int scrollOffset;
    bool refreshNeeded;
} FileBrowser;

// Aligned frame buffers to prevent vertical lines
// Using __attribute__((aligned)) to ensure proper memory alignment
static uint8_t frameBuffer1[SCREEN_WIDTH * SCREEN_HEIGHT * 3 + BUFFER_ALIGNMENT] __attribute__((aligned(BUFFER_ALIGNMENT)));
static uint8_t frameBuffer2[SCREEN_WIDTH * SCREEN_HEIGHT * 3 + BUFFER_ALIGNMENT] __attribute__((aligned(BUFFER_ALIGNMENT)));

// Existing hardware handles
SRAM_HandleTypeDef fmcHandle;
TIM_HandleTypeDef tim3Handle;
MDMA_HandleTypeDef mdmaHandle;
ADC_HandleTypeDef adcHandle;
DMA_HandleTypeDef dmaHandle;
ADC_HandleTypeDef adc2Handle;
DMA_HandleTypeDef dma2Handle;
SAI_HandleTypeDef hsaiHandle;
MMC_HandleTypeDef MMCHandle;

static FileBrowser fileBrowser;

// Menu and application buffers
static uint8_t frameBuffers[2][SCREEN_WIDTH * SCREEN_HEIGHT * 3]; // Two full screen buffers
static uint8_t* frontBuffer;  // Currently displayed buffer
static uint8_t* backBuffer;   // Buffer being drawn to
static bool bufferReady = false;  // Flag to indicate back buffer is ready to swap
static Menu mainMenu;
static AppType currentApp = APP_COUNT; // No app running initially
static uint32_t lastFrameTime = 0;     // For frame rate control
static uint8_t qrBuffer[SCREEN_WIDTH * SCREEN_HEIGHT * 3];
// UI state
static int selectedSample = 0;
static int displayVolume = 100;
static bool volumeMode = false;

// Function prototypes
static void DMA_Init(void);
static void FMC_Init(void);
static void TIM3_Init(void);
static void MDMA_Init(void);
static void ADC_Init(void);
static void hsv2rgb(float h, float s, float v, float *r, float *g, float *b);
static void CRYP_AES_CTR_Encrypt(uint32_t *counter, void *outData, uint32_t outSize, const void *inData, uint32_t inSize);
static void DrawPlayerUI(uint8_t* buffer);
static void DrawPlayButton(uint8_t* buffer, int x, int y, bool isPlaying);
static void DrawStopButton(uint8_t* buffer, int x, int y);
static void DrawPauseButton(uint8_t* buffer, int x, int y);
static void DrawVolumeButton(uint8_t* buffer, int x, int y);
static void DrawProgressBar(uint8_t* buffer, int x, int y, int width, int progress);
static void DrawVolumeBar(uint8_t* buffer, int x, int y, int width, int volume);
static void DrawSampleSelector(uint8_t* buffer, int x, int y, int width, int height);
static void get_aes_data(aes_data_t *aesData);
static void generate_qr(const void *data, size_t size, uint8_t *screenBuffer);
static HAL_StatusTypeDef CRYP_ProcessData(uint32_t* outData, uint32_t outCount, const uint32_t* inData, uint32_t inCount);
bool BOOT_PlaySound(void);
void BOOT_WaitForSound(uint32_t timeout_ms);
static void FileBrowser_HandleInput(void);
static void FileBrowser_Init(void);

// interesting... https://www.keil.com/support/docs/3777%20%20.htm

static HAL_StatusTypeDef CRYP_ProcessData(uint32_t* outData, uint32_t outCount, const uint32_t* inData, uint32_t inCount)
{
    uint32_t timeout = HAL_GetTick() + 1000; // 1 second timeout
    
    // Enable CRYP
    CRYP->CR |= CRYP_CR_CRYPEN;

    uint32_t inOffset = 0;
    uint32_t outOffset = 0;
    while (outOffset < outCount) {
        // Check for timeout
        if (HAL_GetTick() > timeout) {
            // Disable CRYP and flush
            CRYP->CR &= ~CRYP_CR_CRYPEN;
            CRYP->CR |= CRYP_CR_FFLUSH;
            return HAL_TIMEOUT;
        }
        
        // Push data if input FIFO isn't full
        while (inOffset < inCount && (CRYP->SR & CRYP_SR_IFNF)) {
            CRYP->DIN = inData[inOffset++];
        }

        // Read data if output FIFO isn't empty
        while (CRYP->SR & CRYP_SR_OFNE) {
            outData[outOffset++] = CRYP->DOUT;
            // Reset timeout on successful data processing
            timeout = HAL_GetTick() + 1000;
        }
        
        // Small delay to prevent CPU hogging in case of hardware issues
        if (inOffset == inCount && !(CRYP->SR & CRYP_SR_OFNE)) {
            HAL_Delay(1);
        }
    }

    // Disable CRYP
    CRYP->CR &= ~CRYP_CR_CRYPEN;

    // Flush CRYP
    CRYP->CR |= CRYP_CR_FFLUSH;
    
    return HAL_OK;
}

// Double buffering functions
static void SwapBuffers(void)
{
    uint8_t* temp = frontBuffer;
    frontBuffer = backBuffer;
    backBuffer = temp;

    // Flush cache for DMA transfer
    SCB_CleanDCache_by_Addr((uint32_t*)frontBuffer, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
    
    // Transfer the front buffer to the display using DMA if possible
    LCD_DrawScreenBuffer(frontBuffer, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
    
    bufferReady = false;
}

// Test pattern generator
static void GenerateTestPattern(uint8_t* buffer)
{
    // Clear buffer first
    LCD_ClearBuffer(buffer, 0, 0, 0);
    
    // Draw a series of horizontal color bars
    int barHeight = SCREEN_HEIGHT / 6;
    
    // Red bar
    LCD_DrawRectToBuffer(buffer, 0, 0, SCREEN_WIDTH, barHeight, 255, 0, 0);
    
    // Green bar
    LCD_DrawRectToBuffer(buffer, 0, barHeight, SCREEN_WIDTH, barHeight, 0, 255, 0);
    
    // Blue bar
    LCD_DrawRectToBuffer(buffer, 0, barHeight*2, SCREEN_WIDTH, barHeight, 0, 0, 255);
    
    // Yellow bar
    LCD_DrawRectToBuffer(buffer, 0, barHeight*3, SCREEN_WIDTH, barHeight, 255, 255, 0);
    
    // Cyan bar
    LCD_DrawRectToBuffer(buffer, 0, barHeight*4, SCREEN_WIDTH, barHeight, 0, 255, 255);
    
    // Magenta bar
    LCD_DrawRectToBuffer(buffer, 0, barHeight*5, SCREEN_WIDTH, barHeight, 255, 0, 255);
    
    // White circle in the center
    int centerX = SCREEN_WIDTH / 2;
    int centerY = SCREEN_HEIGHT / 2;
    int radius = SCREEN_HEIGHT / 4;
    
    LCD_DrawCircleToBuffer(buffer, centerX, centerY, radius, 255, 255, 255);
}

static void GenerateCheckerboard(uint8_t* buffer)
{
    // Clear buffer first
    LCD_ClearBuffer(buffer, 0, 0, 0);
    
    // Use large checker pattern (larger blocks minimize the chance of vertical lines)
    int blockSize = 20;  // Larger blocks
    
    for (int y = 0; y < SCREEN_HEIGHT; y += blockSize) {
        for (int x = 0; x < SCREEN_WIDTH; x += blockSize) {
            // Determine if this block should be white
            int isWhite = ((x / blockSize) + (y / blockSize)) % 2;
            
            // Fill this block with appropriate color
            uint8_t color = isWhite ? 255 : 0;
            
            // Draw the block as a rectangle
            LCD_DrawRectToBuffer(buffer, x, y, 
                     (x + blockSize > SCREEN_WIDTH) ? SCREEN_WIDTH - x : blockSize,
                     (y + blockSize > SCREEN_HEIGHT) ? SCREEN_HEIGHT - y : blockSize,
                     color, color, color);
        }
    }
}

// Menu function prototypes
static void Menu_Init(void);
static void Menu_Draw(uint8_t* buffer);
static void Menu_HandleInput(void);
static void Menu_SelectItem(int index);
static void Menu_RunApp(AppType app);

// App handler prototypes
static void App_CatViewer(uint8_t* buffer);
static void App_QRCode(uint8_t* buffer);
static void App_ColorWheel(uint8_t* buffer);
static void App_Settings(uint8_t* buffer);
static void App_CatPng(uint8_t* buffer);
static void App_QrDisplay(uint8_t* buffer);
static void App_ButterPng(uint8_t* buffer);
static void App_FileBrowser(uint8_t* buffer);
static void MMC_ReadTest(uint8_t* buffer);

int main(void)
{
    // System initialization (from original code)
    SCB_EnableICache();
    SCB_EnableDCache();
    __enable_irq();

    HAL_Init();

    // Peripheral initialization
    DMA_Init();
    FMC_Init();
    MDMA_Init();
    TIM3_Init();
    ADC_Init();
    INPUT_Init();
    LCD_Init();

    if (AUDIO_Init()) {
        /* Play boot sound */
        if (BOOT_PlaySound()) {
            /* Optional: Wait for boot sound to complete (max 3 seconds) */
            BOOT_WaitForSound(3000);
        }
    }

    // Get LCD ID
    uint32_t lcdId = 0;
    LCD_RDID(&lcdId);

    // Setup backlight
    LCD_SetBrightness(1.0f);

    // Initialize double-buffering system
    //frontBuffer = frameBuffers[0]; old
    //backBuffer = frameBuffers[1]; old
    // Initialize double-buffering system with aligned addresses
    frontBuffer = (uint8_t*)(((uintptr_t)frameBuffer1 + BUFFER_ALIGNMENT - 1) & ~(BUFFER_ALIGNMENT - 1));
    backBuffer = (uint8_t*)(((uintptr_t)frameBuffer2 + BUFFER_ALIGNMENT - 1) & ~(BUFFER_ALIGNMENT - 1));
    
    // Clear both buffers to black
    LCD_ClearBuffer(frontBuffer, 0, 0, 0);
    LCD_ClearBuffer(backBuffer, 0, 0, 0);
    
    // Initialize menu system
    Menu_Init();
    
    // Show the menu on both buffers initially
    Menu_Draw(frontBuffer);
    Menu_Draw(backBuffer);

    // Turn on the display
    LCD_DISPON();
    
    // Initial display update
    LCD_DrawScreenBuffer(frontBuffer, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
    
    // Reset timer
    lastFrameTime = HAL_GetTick();

    FileBrowser_Init();

    // Generate QR code
    aes_data_t aesData;
    get_aes_data(&aesData);
    generate_qr(&aesData, sizeof(aesData), qrBuffer);

    // Main loop
    float lastDial = INPUT_GetDial();
    uint32_t lastButtons = 0;
    uint32_t buttonCooldown = 0;
    bool needsRedraw = false;

    static uint32_t lastDialTime = 0;    // Time of last dial movement
    static float dialAccumulator = 0.0f; // Accumulate small movements
    static float previousDial = -1.0f;   // Previous dial position, -1 means uninitialized
    
    while (1) {
        uint32_t currentTime = HAL_GetTick();
        uint32_t buttons = INPUT_GetButtons();
        float dial = INPUT_GetDial();
        AUDIO_Process();
        
        // Handle button debouncing
        if (buttonCooldown > 0) {
            buttonCooldown--;
            lastButtons = buttons;
            HAL_Delay(1);
            continue;
        }
        
        // Check for new button presses
        uint32_t newPresses = buttons & ~lastButtons;
        
        // Only process inputs if not in an app or if the back button is pressed to return to menu
        if (currentApp == APP_COUNT || (newPresses & BUTTON_BACK)) {
            // Return to menu if in an app
            if (currentApp != APP_COUNT && (newPresses & BUTTON_BACK)) {
                currentApp = APP_COUNT;
                Menu_Draw(backBuffer);
                needsRedraw = true;
                buttonCooldown = 20; // 200ms debounce
            }
            // Otherwise handle menu navigation
            else if (currentApp == APP_COUNT) {
                // Dial button selects menu item
                if (newPresses & BUTTON_DIAL) {
                    Menu_SelectItem(mainMenu.selectedIndex);
                    buttonCooldown = 20; // 200ms debounce
                    needsRedraw = true;
                }
                
                // Initialize previousDial if needed
                if (previousDial < 0) {
                    previousDial = dial;
                }
                
                // Calculate the delta, handling the wrap-around at 360 degrees
                float dialDelta = dial - previousDial;
                
                // Handle wrap-around (crossing from 0 to 360 or vice versa)
                if (dialDelta > 180.0f) {
                    dialDelta -= 360.0f;
                } else if (dialDelta < -180.0f) {
                    dialDelta += 360.0f;
                }
                
                // Add to accumulator
                dialAccumulator += dialDelta;
                
                // Process accumulated movement if threshold reached and debounce time passed
                if ((fabs(dialAccumulator) >= DIAL_THRESHOLD) && 
                    (currentTime - lastDialTime >= DIAL_DEBOUNCE_TIME)) {
                    
                    if (dialAccumulator < 0) {
                        // Move selection down (clockwise rotation)
                        if (mainMenu.selectedIndex < mainMenu.itemCount - 1) {
                            mainMenu.selectedIndex++;
                            
                            // Adjust scroll if needed to show selected item
                            if (mainMenu.selectedIndex >= mainMenu.scrollOffset + VISIBLE_MENU_ITEMS) {
                                mainMenu.scrollOffset = mainMenu.selectedIndex - VISIBLE_MENU_ITEMS + 1;
                            }
                        }
                    } else {
                        // Move selection up (counterclockwise rotation)
                        if (mainMenu.selectedIndex > 0) {
                            mainMenu.selectedIndex--;
                            
                            // Adjust scroll if needed to show selected item
                            if (mainMenu.selectedIndex < mainMenu.scrollOffset) {
                                mainMenu.scrollOffset = mainMenu.selectedIndex;
                            }
                        }
                    }
                    
                    // Reset accumulator and update time
                    dialAccumulator = 0.0f;
                    lastDialTime = currentTime;
                    
                    // Redraw menu
                    Menu_Draw(backBuffer);
                    needsRedraw = true;
                }
                
                // Keep track of previous dial position
                previousDial = dial;
            }
        }
        // When in an app, let the app handle its own drawing to the back buffer
        else {
            Menu_RunApp(currentApp);
            needsRedraw = true;
        }
        
        // Update display at controlled frame rate if needed
        if (needsRedraw && (currentTime - lastFrameTime >= FRAME_RATE_MS)) {
            // Wait for previous DMA to finish
            //while (LCD_IsDMABusy()) {
            //    __NOP();
            //}
            SwapBuffers();
            lastFrameTime = currentTime;
            needsRedraw = false;
        }
        
        lastButtons = buttons;
        HAL_Delay(1); // Small delay to prevent CPU hogging
    }
}

static void MMC_KioxiaTest(uint8_t* buffer) {
    const struct mf_font_s* font = TEXT_GetDefaultFont();
    static char resultText[512] = "Press DIAL to initialize KIOXIA eMMC";
    static bool testPerformed = false;
    static HAL_StatusTypeDef initStatus = HAL_ERROR;
    
    // Clear buffer with dark background
    LCD_ClearBuffer(buffer, 20, 20, 40);
    
    // Draw title bar
    LCD_DrawRectToBuffer(buffer, 0, 0, SCREEN_WIDTH, MENU_TITLE_HEIGHT, 40, 40, 80);
    TEXT_DrawAlignedToBuffer(buffer, font, SCREEN_WIDTH / 2, 
                            (MENU_TITLE_HEIGHT - font->line_height) / 2, 
                            MF_ALIGN_CENTER, "KIOXIA eMMC Test", 255, 255, 255);
    
    // Handle input
    uint32_t buttons = INPUT_GetButtons();
    static uint32_t lastButtons = 0;
    uint32_t newPresses = buttons & ~lastButtons;
    
    if (newPresses & BUTTON_DIAL) {
        // Step 1: Reset SDMMC peripheral
        __HAL_RCC_SDMMC1_FORCE_RESET();
        HAL_Delay(10);
        __HAL_RCC_SDMMC1_RELEASE_RESET();
        HAL_Delay(10);
        
        // Step 2: Configure GPIO pins for SDMMC
        // Note: This should already be done in your hardware initialization
        // but explicitly configuring it here might help
        
        // Step 3: De-initialize MMC handle
        HAL_MMC_DeInit(&MMCHandle);
        HAL_Delay(10);
        
        // Step 4: Configure MMC with very low speed for initialization
        MMCHandle.Instance = SDMMC1;
        MMCHandle.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
        MMCHandle.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
        MMCHandle.Init.BusWide = SDMMC_BUS_WIDE_1B; // Start with 1-bit width
        MMCHandle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
        MMCHandle.Init.ClockDiv = 198; // Very slow clock (approx. 1 MHz) for initial communication
        
        // Step 5: Initialize with extended timeout
        uint32_t tickstart = HAL_GetTick();
        initStatus = HAL_MMC_Init(&MMCHandle);
        uint32_t initTime = HAL_GetTick() - tickstart;
        
        if (initStatus == HAL_OK) {
            // Step 6: Initialize the card with extended timeout
            HAL_StatusTypeDef cardStatus = HAL_MMC_InitCard(&MMCHandle);
            
            // Step 7: If successful, try to gradually speed up
            HAL_StatusTypeDef speedStatus = HAL_ERROR;
            if (cardStatus == HAL_OK) {
                // Try progressively faster clock speeds
                const uint32_t speedSteps[] = {49, 24, 9, 4, 2}; // ClockDiv values for ~4, 8, 20, 40, 50 MHz
                uint32_t successfulDivider = 198; // Default to the initial slow speed
                
                for (int i = 0; i < sizeof(speedSteps)/sizeof(speedSteps[0]); i++) {
                    // Save the current settings
                    MMCHandle.Init.ClockDiv = speedSteps[i];
                    
                    // Try to initialize at this speed
                    if (HAL_MMC_InitCard(&MMCHandle) == HAL_OK) {
                        successfulDivider = speedSteps[i];
                        speedStatus = HAL_OK;
                        // Successfully set this speed, continue to try faster speeds
                    } else {
                        // This speed failed, revert to the last successful speed and stop
                        MMCHandle.Init.ClockDiv = successfulDivider;
                        HAL_MMC_InitCard(&MMCHandle);
                        break;
                    }
                }
                
                // Step 8: Try to set bus width (4-bit or 8-bit)
                HAL_StatusTypeDef wideStatus = HAL_ERROR;
                
                // Try 8-bit first (typical for eMMC)
                wideStatus = HAL_MMC_ConfigWideBusOperation(&MMCHandle, SDMMC_BUS_WIDE_8B);
                
                // If 8-bit fails, try 4-bit
                if (wideStatus != HAL_OK) {
                    wideStatus = HAL_MMC_ConfigWideBusOperation(&MMCHandle, SDMMC_BUS_WIDE_4B);
                }
                
                // Step 9: Format the initialization result
                float clockMhz = 200.0f / (MMCHandle.Init.ClockDiv + 2); // Assuming SDMMC_CK is 200MHz
                
                snprintf(resultText, sizeof(resultText), 
                        "INIT SUCCESS\n\n"
                        "Init Time: %lu ms\n"
                        "Card State: %d (%s)\n"
                        "Card Type: 0x%08lX\n"
                        "Card Class: 0x%08lX\n"
                        "Capacity: %lu MB\n"
                        "Block Size: %lu bytes\n"
                        "Bus Width: %s\n"
                        "Clock: %.1f MHz",
                        initTime,
                        (int)HAL_MMC_GetCardState(&MMCHandle),
                        HAL_MMC_GetCardState(&MMCHandle) == HAL_MMC_CARD_TRANSFER ? "READY" : "NOT READY",
                        MMCHandle.MmcCard.CardType,
                        MMCHandle.MmcCard.Class,
                        (MMCHandle.MmcCard.BlockNbr * MMCHandle.MmcCard.BlockSize) / (1024*1024),
                        MMCHandle.MmcCard.BlockSize,
                        wideStatus == HAL_OK ? 
                            (MMCHandle.Init.BusWide == SDMMC_BUS_WIDE_8B ? "8-bit" : "4-bit") : 
                            "1-bit",
                        clockMhz);
                
                // Step 10: Now try to read the first sector
                uint8_t testSector[512];
                HAL_StatusTypeDef readStatus = HAL_MMC_ReadBlocks(&MMCHandle, testSector, 0, 1, 2000);
                
                char readResult[128];
                if (readStatus == HAL_OK) {
                    // Check for boot signature and filesystem
                    bool hasBootSig = (testSector[510] == 0x55 && testSector[511] == 0xAA);
                    
                    // Look for FAT signature
                    bool hasFatSig = false;
                    if (testSector[0x36] == 'F' && testSector[0x37] == 'A' && testSector[0x38] == 'T') {
                        hasFatSig = true; // FAT16
                    } else if (testSector[0x52] == 'F' && testSector[0x53] == 'A' && testSector[0x54] == 'T' && 
                              testSector[0x55] == '3' && testSector[0x56] == '2') {
                        hasFatSig = true; // FAT32
                    }
                    
                    snprintf(readResult, sizeof(readResult), 
                            "\n\nRead Test: SUCCESS\n"
                            "Boot Sig: %s\n"
                            "FAT Sig: %s", 
                            hasBootSig ? "Valid (55AA)" : "Invalid",
                            hasFatSig ? "Found" : "Not found");
                } else {
                    snprintf(readResult, sizeof(readResult), 
                            "\n\nRead Test: FAILED\n"
                            "Error: %d, Code: 0x%08lX", 
                            (int)readStatus, MMCHandle.ErrorCode);
                }
                
                // Append read result
                strcat(resultText, readResult);
                
                // Step 11: Try to mount filesystem
                if (readStatus == HAL_OK) {
                    FATFS fs;
                    FRESULT fr = f_mount(&fs, "0:", 1);
                    
                    char mountResult[128];
                    snprintf(mountResult, sizeof(mountResult), 
                            "\n\nFatFS Mount: %s (%d)",
                            fr == FR_OK ? "SUCCESS" : "FAILED", (int)fr);
                    
                    // Append mount result
                    strcat(resultText, mountResult);
                    
                    // If mount successful, try to open root directory
                    if (fr == FR_OK) {
                        DIR dir;
                        FRESULT fr2 = f_opendir(&dir, "0:/");
                        
                        char dirResult[128];
                        snprintf(dirResult, sizeof(dirResult), 
                                "\nRoot Dir: %s (%d)",
                                fr2 == FR_OK ? "ACCESSIBLE" : "FAILED", (int)fr2);
                        
                        // Append directory result
                        strcat(resultText, dirResult);
                        
                        if (fr2 == FR_OK) {
                            f_closedir(&dir);
                        }
                    }
                }
            } else {
                // Card initialization failed
                snprintf(resultText, sizeof(resultText), 
                        "MMC INIT OK BUT CARD INIT FAILED\n\n"
                        "Init Time: %lu ms\n"
                        "HAL_MMC_InitCard returned: %d\n"
                        "Error Code: 0x%08lX\n"
                        "Card State: %d",
                        initTime,
                        (int)cardStatus,
                        MMCHandle.ErrorCode,
                        (int)HAL_MMC_GetCardState(&MMCHandle));
            }
        } else {
            // Initialization failed
            snprintf(resultText, sizeof(resultText), 
                    "INIT FAILED\n\n"
                    "Init Time: %lu ms\n"
                    "HAL_MMC_Init returned: %d\n"
                    "Error Code: 0x%08lX\n"
                    "Card State: %d\n\n"
                    "Manufacturer: KIOXIA\n"
                    "Model: THGBMTG5D1LBAIL\n\n"
                    "Check hardware connections.",
                    initTime,
                    (int)initStatus,
                    MMCHandle.ErrorCode,
                    (int)HAL_MMC_GetCardState(&MMCHandle));
        }
        
        testPerformed = true;
    }
    
    // Display result text with word wrapping
    TEXT_DrawWrappedToBuffer(buffer, font, 20, MENU_TITLE_HEIGHT + 20, 
                           SCREEN_WIDTH - 40, resultText, 255, 255, 255);
    
    // Display instruction text at bottom
    TEXT_DrawAlignedToBuffer(buffer, font, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 40, 
                           MF_ALIGN_CENTER, "Press BACK to return to menu", 200, 200, 200);
    
    lastButtons = buttons;
}

// Menu system implementation
static void Menu_Init(void)
{
    mainMenu.title = "Homebrew Menu";
    mainMenu.itemCount = 0;
    mainMenu.selectedIndex = 0;
    mainMenu.scrollOffset = 0;  // Initialize scroll position
    mainMenu.active = true;
    
    // Add Cat Viewer app #2
    MenuItem catItem = {
        .title = "Pattern Viewer",
        .type = APP_CAT_VIEWER,
        .handler = App_CatViewer,
        .r = 240, .g = 160, .b = 80
    };
    mainMenu.items[mainMenu.itemCount++] = catItem;
    
    // Add QR Code app
    /*MenuItem qrItem = {
        .title = "Checker Pattern",
        .type = APP_QR_CODE,
        .handler = App_QRCode,
        .r = 80, .g = 80, .b = 240
    };
    mainMenu.items[mainMenu.itemCount++] = qrItem;*/
    
    // Add Color Wheel app
    MenuItem colorItem = {
        .title = "Color Wheel",
        .type = APP_COLOR_WHEEL,
        .handler = App_ColorWheel,
        .r = 240, .g = 80, .b = 240
    };
    mainMenu.items[mainMenu.itemCount++] = colorItem;
    
    // Add Settings app #1
    MenuItem settingsItem = {
        .title = "Brightness",
        .type = APP_SETTINGS,
        .handler = App_Settings,
        .r = 180, .g = 180, .b = 180
    };
    mainMenu.items[mainMenu.itemCount++] = settingsItem;

    // Add QR Code viewer app
    MenuItem qrViewItem = {
        .title = "QR Code",
        .type = APP_QR_DISPLAY,
        .handler = App_QrDisplay,
        .r = 255, .g = 255, .b = 255
    };
    mainMenu.items[mainMenu.itemCount++] = qrViewItem;

    // Add Cat PNG viewer app
    MenuItem catPngItem = {
        .title = "Cat Image",
        .type = APP_CAT_PNG,
        .handler = App_CatPng,
        .r = 255, .g = 120, .b = 30
    };
    mainMenu.items[mainMenu.itemCount++] = catPngItem;

    //butta dog
    MenuItem dogPngItem = {
        .title = "Butter Dog",
        .type = APP_BUTTER_PNG,
        .handler = App_ButterPng,
        .r = 255, .g = 225, .b = 129
    };
    mainMenu.items[mainMenu.itemCount++] = dogPngItem;

    // Add File Browser app
    MenuItem fileBrowserItem = {
        .title = "File Browser",
        .type = APP_FILE_BROWSER,
        .handler = App_FileBrowser,
        .r = 60, .g = 180, .b = 120
    };
    mainMenu.items[mainMenu.itemCount++] = fileBrowserItem;

    MenuItem mmcReadItem = {
        .title = "eMMC Read Test",
        .type = APP_MMC_READ_TEST,
        .handler = MMC_ReadTest,
        .r = 100, .g = 180, .b = 220
    };
    mainMenu.items[mainMenu.itemCount++] = mmcReadItem;

    MenuItem kioxiaItem = {
        .title = "KIOXIA eMMC Test",
        .type = APP_MMC_KIOXIA_TEST,
        .handler = MMC_KioxiaTest,
        .r = 100, .g = 220, .b = 180
    };
    mainMenu.items[mainMenu.itemCount++] = kioxiaItem;

    // Add more menu items for testing scrolling
    /*MenuItem extraItem1 = {
        .title = "Extra Item 1",
        .type = APP_QR_DISPLAY,
        .handler = App_QrDisplay,
        .r = 100, .g = 200, .b = 100
    };
    mainMenu.items[mainMenu.itemCount++] = extraItem1;
    
    MenuItem extraItem2 = {
        .title = "Extra Item 2",
        .type = APP_SETTINGS,
        .handler = App_Settings,
        .r = 200, .g = 100, .b = 100
    };
    mainMenu.items[mainMenu.itemCount++] = extraItem2;
    
    MenuItem extraItem3 = {
        .title = "Extra Item 3",
        .type = APP_SETTINGS,
        .handler = App_Settings,
        .r = 100, .g = 100, .b = 200
    };
    mainMenu.items[mainMenu.itemCount++] = extraItem3;*/
}

static void Menu_Draw(uint8_t* buffer)
{
    // Clear screen with dark background
    LCD_ClearBuffer(buffer, 40, 40, 50);
    
    // Draw menu title
    LCD_DrawRectToBuffer(buffer, 0, 0, SCREEN_WIDTH, MENU_TITLE_HEIGHT, 60, 60, 70);
    
    // Get the default font
    const struct mf_font_s* font = TEXT_GetDefaultFont();
    
    // Draw the menu title text centered at the top
    TEXT_DrawAlignedToBuffer(buffer, font, SCREEN_WIDTH / 2, 
                           (MENU_TITLE_HEIGHT - font->line_height) / 2, 
                           MF_ALIGN_CENTER, mainMenu.title, 255, 255, 255);
    
    // Calculate visible items
    int visibleItems = mainMenu.itemCount < VISIBLE_MENU_ITEMS ? mainMenu.itemCount : VISIBLE_MENU_ITEMS;
    
    // Adjust scroll offset if necessary to keep selected item visible
    if (mainMenu.selectedIndex < mainMenu.scrollOffset) {
        mainMenu.scrollOffset = mainMenu.selectedIndex;
    } else if (mainMenu.selectedIndex >= mainMenu.scrollOffset + visibleItems) {
        mainMenu.scrollOffset = mainMenu.selectedIndex - visibleItems + 1;
    }
    
    // Draw scroll up indicator if needed
    if (mainMenu.scrollOffset > 0) {
        // Draw up arrow or indicator
        LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - 15, MENU_TITLE_HEIGHT, 30, SCROLL_INDICATOR_HEIGHT, 100, 100, 120);
        // Draw a simple up arrow
        for (int i = 0; i < 10; i++) {
            LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - i, MENU_TITLE_HEIGHT + SCROLL_INDICATOR_HEIGHT - i - 2, i * 2, 1, 200, 200, 220);
        }
    }
    
    // Draw menu items that are visible
    for (int i = 0; i < visibleItems; i++) {
        int itemIndex = i + mainMenu.scrollOffset;
        if (itemIndex >= mainMenu.itemCount) break;
        
        MenuItem* item = &mainMenu.items[itemIndex];
        int y = MENU_TITLE_HEIGHT + (mainMenu.scrollOffset > 0 ? SCROLL_INDICATOR_HEIGHT : 0) + i * MENU_ITEM_HEIGHT;
        
        // Draw selection background for selected item
        if (itemIndex == mainMenu.selectedIndex) {
            LCD_DrawRectToBuffer(buffer, 0, y, SCREEN_WIDTH, MENU_ITEM_HEIGHT, 80, 80, 100);
        }
        
        // Draw item icon (colored rectangle)
        LCD_DrawRectToBuffer(buffer, MENU_ITEM_PADDING, y + MENU_ITEM_PADDING, 
                            MENU_ITEM_HEIGHT - MENU_ITEM_PADDING*2, 
                            MENU_ITEM_HEIGHT - MENU_ITEM_PADDING*2, 
                            item->r, item->g, item->b);
        
        // Calculate text position (to the right of the colored box)
        int text_x = MENU_ITEM_PADDING * 2 + (MENU_ITEM_HEIGHT - MENU_ITEM_PADDING*2);
        int text_y = y + (MENU_ITEM_HEIGHT - font->line_height) / 2;
        
        // Choose text color based on whether this item is selected
        uint8_t text_r, text_g, text_b;
        if (itemIndex == mainMenu.selectedIndex) {
            // Brighter text for selected item
            text_r = text_g = text_b = 255;
        } else {
            // Less bright text for unselected items
            text_r = text_g = text_b = 220;
        }
        
        // Draw the menu item text
        TEXT_DrawStringToBuffer(buffer, font, text_x, text_y, item->title, text_r, text_g, text_b);
    }
    
    // Draw scroll down indicator if needed
    int scrollDownPos = MENU_TITLE_HEIGHT + 
                      (mainMenu.scrollOffset > 0 ? SCROLL_INDICATOR_HEIGHT : 0) + 
                      visibleItems * MENU_ITEM_HEIGHT;
                      
    if (mainMenu.scrollOffset + visibleItems < mainMenu.itemCount) {
        // Draw down arrow or indicator
        LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - 15, scrollDownPos, 30, SCROLL_INDICATOR_HEIGHT, 100, 100, 120);
        // Draw a simple down arrow
        for (int i = 0; i < 10; i++) {
            LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - i, scrollDownPos + i + 1, i * 2, 1, 200, 200, 220);
        }
    }
}

static void Menu_SelectItem(int index)
{
    if (index >= 0 && index < mainMenu.itemCount) {
        currentApp = mainMenu.items[index].type;
    }
}

static void Menu_RunApp(AppType app)
{
    // Find and run the app handler
    for (int i = 0; i < mainMenu.itemCount; i++) {
        if (mainMenu.items[i].type == app && mainMenu.items[i].handler != NULL) {
            mainMenu.items[i].handler(backBuffer);
            break;
        }
    }
}

// App implementations
static void App_CatViewer(uint8_t* buffer)
{
    // Generate test pattern directly to buffer
    GenerateTestPattern(buffer);
}

static void App_QRCode(uint8_t* buffer)
{
    // Generate checkerboard pattern to buffer
    GenerateCheckerboard(buffer);
}

static void App_ColorWheel(uint8_t* buffer)
{
    static float hue = 0.0f;
    float r, g, b;
    
    // Update hue based on dial position
    float dial = INPUT_GetDial();
    hue = dial / 360.0f;
    
    // Convert HSV to RGB
    hsv2rgb(hue, 1.0f, 1.0f, &r, &g, &b);
    
    // Draw colored screen
    LCD_ClearBuffer(buffer, r * 255, g * 255, b * 255);
}

static void App_Settings(uint8_t* buffer)
{
    static float brightness = 1.0f;
    static float targetBrightness = 1.0f;
    
    // Get dial position (0-360 degrees)
    float dial = INPUT_GetDial();

    // Clamp dial position to 0-360 degrees range
    if (dial < 0.01f) dial = 0.01f;
    if (dial > 360.0f) dial = 360.0f;
    
    // Map dial position to brightness range (0.01 to 1.0)
    targetBrightness = 0.01f + (dial / 270.0f) * 0.99f;
    
    // Clamp values to ensure they stay in range
    if (targetBrightness < 0.01f) targetBrightness = 0.01f;
    if (targetBrightness > 1.0f) targetBrightness = 1.0f;
    
    // Optional: Smooth transition (comment in/out if not desired)
    brightness = brightness * 0.9f +targetBrightness * 0.1f;
    
    // Apply brightness setting
    LCD_SetBrightness(brightness);
    
    // Draw settings screen
    LCD_ClearBuffer(buffer, 50, 50, 60);
    
    // Draw brightness bar
    LCD_DrawRectToBuffer(buffer, 20, 50, SCREEN_WIDTH - 40, 30, 100, 100, 100);
    LCD_DrawRectToBuffer(buffer, 20, 50, (int)((SCREEN_WIDTH - 40) * brightness), 30, 255, 200, 0);
    
    // Display current brightness percentage
    char brightnessText[16];
    sprintf(brightnessText, "%d%%", (int)(brightness * 100));
    TEXT_DrawAlignedToBuffer(buffer, TEXT_GetDefaultFont(), SCREEN_WIDTH / 2,
                            100, MF_ALIGN_CENTER, brightnessText, 255, 255, 255);
}

static void App_CatPng(uint8_t* buffer)
{
    memcpy(buffer, cat_png_data, sizeof(cat_png_data));
}

static void App_ButterPng(uint8_t* buffer)
{
    memcpy(buffer, dog_png_data, sizeof(dog_png_data));
}

static void App_QrDisplay(uint8_t* buffer)
{
    // Generate QR code if not already generated
    static bool qrGenerated = false;
    
    if (!qrGenerated) {
        aes_data_t aesData;
        get_aes_data(&aesData);
        generate_qr(&aesData, sizeof(aesData), qrBuffer);
        qrGenerated = true;
    }
    
    // Copy the QR code to the buffer
    memcpy(buffer, qrBuffer, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
}

// Draw the file browser interface
static void App_FileBrowser(uint8_t* buffer) {
    const struct mf_font_s* font = TEXT_GetDefaultFont();
    int visibleItems = fileBrowser.fileCount < MAX_FILES_PER_PAGE ? fileBrowser.fileCount : MAX_FILES_PER_PAGE;
    int itemHeight = 40;
    int itemPadding = 10;
    bool needsRefresh = fileBrowser.refreshNeeded;
    
    // Create a smaller secondary font for file info/details
    const struct mf_font_s* smallFont = font; // Fallback to same font if can't find a smaller one
    
    // Handle input first
    FileBrowser_HandleInput();
    
    // Only redraw if needed
    if (needsRefresh) {
        // Clear screen with dark background
        LCD_ClearBuffer(buffer, 30, 30, 40);
        
        // Draw title bar
        LCD_DrawRectToBuffer(buffer, 0, 0, SCREEN_WIDTH, MENU_TITLE_HEIGHT, 50, 50, 70);
        
        // Draw current path
        char pathTitle[MAX_PATH_LEN + 10];
        if (strcmp(fileBrowser.currentPath, "0:/") == 0) {
            strcpy(pathTitle, "eMMC Root Directory");
        } else {
            snprintf(pathTitle, sizeof(pathTitle), "Dir: %s", fileBrowser.currentPath);
        }
        
        TEXT_DrawAlignedToBuffer(buffer, font, SCREEN_WIDTH / 2, 
                               (MENU_TITLE_HEIGHT - font->line_height) / 2, 
                               MF_ALIGN_CENTER, pathTitle, 255, 255, 255);
        
        // Draw scroll up indicator if needed
        if (fileBrowser.scrollOffset > 0) {
            LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - 15, MENU_TITLE_HEIGHT, 30, SCROLL_INDICATOR_HEIGHT, 100, 100, 120);
            // Draw a simple up arrow
            for (int i = 0; i < 10; i++) {
                LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - i, MENU_TITLE_HEIGHT + SCROLL_INDICATOR_HEIGHT - i - 2, i * 2, 1, 200, 200, 220);
            }
        }
        
        // Draw file list
        for (int i = 0; i < visibleItems; i++) {
            int fileIndex = i + fileBrowser.scrollOffset;
            if (fileIndex >= fileBrowser.fileCount) break;
            
            FileEntry* file = &fileBrowser.files[fileIndex];
            int y = MENU_TITLE_HEIGHT + (fileBrowser.scrollOffset > 0 ? SCROLL_INDICATOR_HEIGHT : 0) + i * itemHeight;
            
            // Draw selection background for selected item
            if (fileIndex == fileBrowser.selectedIndex) {
                LCD_DrawRectToBuffer(buffer, 0, y, SCREEN_WIDTH, itemHeight, 80, 80, 100);
            }
            
            // Draw file/directory icon
            uint8_t r, g, b;
            if (file->isDirectory) {
                // Folder icon color
                r = 255; g = 200; b = 0;
            } else {
                // File icon color
                r = 100; g = 150; b = 255;
            }
            
            // Draw icon
            LCD_DrawRectToBuffer(buffer, itemPadding, y + itemPadding, 
                                itemHeight - itemPadding*2, 
                                itemHeight - itemPadding*2, 
                                r, g, b);
            
            // Calculate text position
            int text_x = itemPadding * 2 + (itemHeight - itemPadding*2);
            int text_y = y + (itemHeight - font->line_height) / 2;
            
            // Choose text color
            uint8_t text_r, text_g, text_b;
            if (fileIndex == fileBrowser.selectedIndex) {
                // Brighter text for selected item
                text_r = text_g = text_b = 255;
            } else {
                // Less bright text for unselected items
                text_r = text_g = text_b = 220;
            }
            
            // Draw filename
            TEXT_DrawStringToBuffer(buffer, font, text_x, text_y, file->filename, text_r, text_g, text_b);
        }
        
        // Draw scroll down indicator if needed
        int scrollDownPos = MENU_TITLE_HEIGHT + 
                          (fileBrowser.scrollOffset > 0 ? SCROLL_INDICATOR_HEIGHT : 0) + 
                          visibleItems * itemHeight;
                          
        if (fileBrowser.scrollOffset + visibleItems < fileBrowser.fileCount) {
            // Draw down arrow or indicator
            LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - 15, scrollDownPos, 30, SCROLL_INDICATOR_HEIGHT, 100, 100, 120);
            // Draw a simple down arrow
            for (int i = 0; i < 10; i++) {
                LCD_DrawRectToBuffer(buffer, SCREEN_WIDTH / 2 - i, scrollDownPos + i + 1, i * 2, 1, 200, 200, 220);
            }
        }
        
        // Draw file count and system info at bottom
        char countText[64];
        char statusText[64] = "";
        
        // Get filesystem status
        FATFS *fs;
        DWORD fre_clust;
        FRESULT res = f_getfree("0:", &fre_clust, &fs);
        
        if (res == FR_OK) {
            snprintf(countText, sizeof(countText), "%d item%s | %s", 
                    fileBrowser.fileCount, 
                    fileBrowser.fileCount != 1 ? "s" : "",
                    res == FR_OK ? "FatFS OK" : "FatFS Error");
        } else {
            snprintf(countText, sizeof(countText), "%d item%s | FatFS Error %d", 
                    fileBrowser.fileCount, 
                    fileBrowser.fileCount != 1 ? "s" : "",
                    (int)res);
        }
        
        TEXT_DrawAlignedToBuffer(buffer, smallFont, SCREEN_WIDTH / 2, SCREEN_HEIGHT - font->line_height - 5, 
                               MF_ALIGN_CENTER, countText, 180, 180, 180);
        
        // Reset refresh flag
        fileBrowser.refreshNeeded = false;
    }
}

// o shit...
static void MMC_ReadTest(uint8_t* buffer) {
    const struct mf_font_s* font = TEXT_GetDefaultFont();
    static uint8_t sectorBuffer[512]; // Buffer for a single sector
    static bool readPerformed = false;
    static char resultText[512] = "Press DIAL to read first sector";
    static HAL_StatusTypeDef readStatus = HAL_ERROR;
    
    // Clear buffer with dark background
    LCD_ClearBuffer(buffer, 20, 20, 40);
    
    // Draw title bar
    LCD_DrawRectToBuffer(buffer, 0, 0, SCREEN_WIDTH, MENU_TITLE_HEIGHT, 40, 40, 80);
    TEXT_DrawAlignedToBuffer(buffer, font, SCREEN_WIDTH / 2, 
                            (MENU_TITLE_HEIGHT - font->line_height) / 2, 
                            MF_ALIGN_CENTER, "eMMC Read Test", 255, 255, 255);
    
    // Handle input
    uint32_t buttons = INPUT_GetButtons();
    static uint32_t lastButtons = 0;
    uint32_t newPresses = buttons & ~lastButtons;
    
    if (newPresses & BUTTON_DIAL) {
        // Clear previous results
        memset(sectorBuffer, 0, sizeof(sectorBuffer));
        
        // Try to read first sector (MBR or boot sector)
        readStatus = HAL_MMC_ReadBlocks(&MMCHandle, sectorBuffer, 0, 1, 1000);
        
        // Format result text
        if (readStatus == HAL_OK) {
            // Read successful, check what we got
            char hexDump[384] = {0}; // Buffer for hex dump
            char asciiDump[128] = {0}; // Buffer for ASCII representation
            
            // Generate hex dump of the first 64 bytes
            for (int i = 0; i < 64; i++) {
                char hexByte[4];
                sprintf(hexByte, "%02X ", sectorBuffer[i]);
                strcat(hexDump, hexByte);
                
                // Add newline every 16 bytes
                if ((i + 1) % 16 == 0 && i < 63) {
                    strcat(hexDump, "\n");
                }
                
                // Add ASCII character if printable, otherwise a dot
                char asciiChar[2] = {0};
                asciiChar[0] = (sectorBuffer[i] >= 32 && sectorBuffer[i] <= 126) ? sectorBuffer[i] : '.';
                strcat(asciiDump, asciiChar);
                
                // Add newline every 16 characters
                if ((i + 1) % 16 == 0 && i < 63) {
                    strcat(asciiDump, "\n");
                }
            }
            
            // Check for boot signature (0x55 0xAA at bytes 510-511)
            bool hasBootSignature = (sectorBuffer[510] == 0x55 && sectorBuffer[511] == 0xAA);
            
            // Check for NTFS signature ("NTFS    " at byte 3)
            bool isNTFS = (memcmp(sectorBuffer + 3, "NTFS    ", 8) == 0);
            
            // Check for FAT signature ("FAT" at byte 54 or "FAT32" at byte 82)
            bool isFAT = (memcmp(sectorBuffer + 54, "FAT", 3) == 0 || 
                          memcmp(sectorBuffer + 82, "FAT32", 5) == 0);
            
            // Format the result text with analysis
            snprintf(resultText, sizeof(resultText), 
                    "READ SUCCESS\n\nHex: \n%s\n\nASCII:\n%s\n\nAnalysis: %s%s%s", 
                    hexDump, 
                    asciiDump,
                    hasBootSignature ? "Valid boot signature (55AA)\n" : "No boot signature!\n",
                    isNTFS ? "NTFS signature found\n" : "",
                    isFAT ? "FAT signature found\n" : "");
        } else {
            // Read failed
            snprintf(resultText, sizeof(resultText), 
                    "READ FAILED\nHAL_MMC_ReadBlocks returned: %d\n\n"
                    "MMC Card State: %d\n"
                    "MMC Error Code: 0x%08lX", 
                    (int)readStatus,
                    (int)HAL_MMC_GetCardState(&MMCHandle),
                    MMCHandle.ErrorCode);
        }
        
        readPerformed = true;
    }
    
    // Display result text with word wrapping
    TEXT_DrawWrappedToBuffer(buffer, font, 20, MENU_TITLE_HEIGHT + 20, 
                           SCREEN_WIDTH - 40, resultText, 255, 255, 255);
    
    // Display instruction text at bottom
    TEXT_DrawAlignedToBuffer(buffer, font, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 40, 
                           MF_ALIGN_CENTER, "Press BACK to return to menu", 200, 200, 200);
    
    lastButtons = buttons;
}



static void hsv2rgb(float h, float s, float v, float *r, float *g, float *b)
{
    float p, q, t, fract;
    int i;

    i = (int) floor(h * 6); 
	fract = h * 6.0f - i;
	p = v * (1.0f - s);
	q = v * (1.0f - fract * s);
	t = v * (1.0f - (1 - fract) * s);

	switch (i % 6) {
		case 0:
            *r = v; *g = t; *b = p;
            break;
		case 1:
            *r = q; *g = v; *b = p;
            break;
		case 2:
            *r = p; *g = v; *b = t;
            break;
		case 3:
            *r = p; *g = q; *b = v;
            break;
		case 4:
            *r = t; *g = p; *b = v;
            break;
		case 5:
            *r = v; *g = p; *b = q;
            break;
	}
}

static void CRYP_AES_CTR_Encrypt(uint32_t *counter, void *outData, uint32_t outSize, const void *inData, uint32_t inSize)
{
    // Setup datatype and algomode (AES-CTR)
    CRYP->CR &= ~(CRYP_CR_DATATYPE_0 | CRYP_CR_ALGOMODE_0 | CRYP_CR_ALGOMODE_1 | CRYP_CR_ALGOMODE_2);
    CRYP->CR |= CRYP_CR_ALGOMODE_AES_CTR;

    // Setup direction (Encrypt)
    CRYP->CR &= ~CRYP_CR_ALGODIR;

    // Setup key size (128)
    CRYP->CR &= ~CRYP_CR_KEYSIZE;

    // Clear lowest IV word
    CRYP->IV1RR = 0;

    CRYP_ProcessData((uint32_t*)outData, outSize / 4, (const uint32_t*)inData, inSize / 4);
}

static void get_aes_data(aes_data_t *aesData)
{
    // store IV
    aesData->iv[0] = __builtin_bswap32(CRYP->IV0LR);
    aesData->iv[1] = __builtin_bswap32(CRYP->IV0RR);
    aesData->iv[2] = __builtin_bswap32(CRYP->IV1LR);
    aesData->iv[3] = 0; // for the counter just store 0

    // Clear zero buffer
    uint32_t zeroes[4] = { 0 };

    for (int i = 0; i < 4; i++) {
        // Start blanking out parts of the key after the first round
        if (i > 0) {
            (&CRYP->K0LR)[4 + (i - 1)] = 0;
        }

        // Encrypt zeroes
        uint32_t counter = 0;
        CRYP_AES_CTR_Encrypt(&counter, aesData->encrypted_parts[i], sizeof(aesData->encrypted_parts[i]), zeroes, sizeof(zeroes));
    }
}

static void generate_qr(const void *data, size_t size, uint8_t *screenBuffer)
{
    char* hexBuffer = malloc(size*2 + 1);
    if (!hexBuffer) {
        memset(screenBuffer, 0xFF, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
        return;
    }

    char* hexPtr = hexBuffer;
    for (size_t i = 0; i < size; i++) {
        hexPtr += sprintf(hexPtr, "%02x", *((uint8_t *)data + i));
    }

    uint8_t qr0[qrcodegen_BUFFER_LEN_MAX];
    uint8_t tempBuffer[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(hexBuffer, tempBuffer, qr0, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        free(hexBuffer);
        memset(screenBuffer, 0xFF, SCREEN_WIDTH * SCREEN_HEIGHT * 3);
        return;
    }

    free(hexBuffer);

    uint32_t offset = 0;
    for (int x = SCREEN_WIDTH - 1; x >= 0; x--) {
        for (int y = 0; y < SCREEN_HEIGHT; y++) {
            uint8_t color = 255;

            // Get color and scale up qr code
            if (qrcodegen_getModule(qr0, x/4 - 5, y/4 - 4)) {
                color = 0;
            }

            screenBuffer[offset++] = color;
            screenBuffer[offset++] = color;
            screenBuffer[offset++] = color;
        }
    }
}

/**
 * Play boot sound
 * 
 * @return True if boot sound played successfully
 */
bool BOOT_PlaySound(void) 
{
    // Path to the boot sound file
    const char* bootSoundFile = "boot.shaa";
    
    // Initialize file system if not already initialized
    FATFS fs;
    FRESULT fr;
    
    // Try to mount the filesystem (if not already mounted)
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        // If mount fails, return false but continue boot
        return false;
    }
    
    // Check if boot sound file exists
    FIL file;
    fr = f_open(&file, bootSoundFile, FA_READ);
    if (fr != FR_OK) {
        // Boot sound file not found, return false but continue boot
        return false;
    }
    
    // Close the file, we just wanted to check if it exists
    f_close(&file);
    
    // Load and play the boot sound
    if (AUDIO_Load(bootSoundFile)) {
        // Start playing (non-looping)
        return AUDIO_Play(false);
    }
    
    return false;
}

/**
 * Wait for boot sound to complete
 * 
 * @param timeout_ms Maximum time to wait in milliseconds
 */
void BOOT_WaitForSound(uint32_t timeout_ms)
{
    uint32_t start_tick = HAL_GetTick();
    
    // Wait for audio to finish playing or timeout
    while (AUDIO_GetState() == AUDIO_STATE_PLAYING) {
        // Process audio
        AUDIO_Process();
        
        // Check for timeout
        if ((HAL_GetTick() - start_tick) > timeout_ms) {
            // Timeout occurred, stop audio and return
            AUDIO_Stop();
            break;
        }
        
        // Small delay to not hog CPU
        HAL_Delay(1);
    }
}

//file dir shit

// Function to scan directory and populate file list
static void FileBrowser_ScanDirectory(const char* path) {
    DIR dir;
    FILINFO fno;
    FRESULT fr;
    
    // Clear current file list
    fileBrowser.fileCount = 0;
    fileBrowser.selectedIndex = 0;
    fileBrowser.scrollOffset = 0;
    
    // Update current path (handle root path specially)
    if (strcmp(path, "") == 0 || strcmp(path, "/") == 0 || strcmp(path, "0:/") == 0) {
        strcpy(fileBrowser.currentPath, "0:/");
    } else {
        strncpy(fileBrowser.currentPath, path, MAX_PATH_LEN - 1);
        fileBrowser.currentPath[MAX_PATH_LEN - 1] = '\0';
    }
    
    // Special case: Add ".." for parent directory if not at root
    if (strcmp(path, "") != 0 && strcmp(path, "/") != 0 && strcmp(path, "0:/") != 0) {
        strncpy(fileBrowser.files[fileBrowser.fileCount].filename, "..", MAX_FILENAME_LEN - 1);
        fileBrowser.files[fileBrowser.fileCount].isDirectory = true;
        fileBrowser.fileCount++;
    }
    
    // Always add an info entry showing filesystem statistics
    FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;
    
    // Get drive information
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    
    if (res == FR_OK) {
        // Get total sectors and free sectors
        tot_sect = (fs->n_fatent - 2) * fs->csize;
        fre_sect = fre_clust * fs->csize;
        
        // Format information about free space
        sprintf(fileBrowser.files[fileBrowser.fileCount].filename, 
                "Free: %lu KB / %lu KB", 
                fre_sect / 2, tot_sect / 2);
        fileBrowser.files[fileBrowser.fileCount].isDirectory = false;
        fileBrowser.fileCount++;
    }
    
    // Open directory
    fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        // Could not open directory, create a dummy file to show error
        strncpy(fileBrowser.files[fileBrowser.fileCount].filename, "ERROR: Could not open dir", MAX_FILENAME_LEN - 1);
        fileBrowser.files[fileBrowser.fileCount].isDirectory = false;
        fileBrowser.fileCount++;
        return;
    }
    
    // Read directory contents
    while (1) {
        fr = f_readdir(&dir, &fno);
        
        // Break on error or end of directory
        if (fr != FR_OK || fno.fname[0] == 0) {
            break;
        }
        
        // Skip "." directory
        if (strcmp(fno.fname, ".") == 0) {
            continue;
        }
        
        // Check if we've reached maximum file count
        if (fileBrowser.fileCount >= (MAX_FILES_PER_PAGE * 2)) {
            break;
        }
        
        // Copy filename and file type
        strncpy(fileBrowser.files[fileBrowser.fileCount].filename, fno.fname, MAX_FILENAME_LEN - 1);
        fileBrowser.files[fileBrowser.fileCount].filename[MAX_FILENAME_LEN - 1] = '\0';
        fileBrowser.files[fileBrowser.fileCount].isDirectory = (fno.fattrib & AM_DIR) ? true : false;
        
        // Increment file count
        fileBrowser.fileCount++;
    }
    
    // Close directory
    f_closedir(&dir);
}


// Initialize the file browser
static void FileBrowser_Init(void) {
    // Initialize browser state
    memset(&fileBrowser, 0, sizeof(FileBrowser));
    
    // Set initial path to root
    strcpy(fileBrowser.currentPath, "");
    
    // Try to mount the filesystem first
    FATFS fs;
    FRESULT fr;
    
    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) {
        // Could not mount filesystem, create error message
        fileBrowser.fileCount = 0;
        snprintf(fileBrowser.files[fileBrowser.fileCount].filename, MAX_FILENAME_LEN - 1, 
                "Mount Error: %d", (int)fr);
        fileBrowser.files[fileBrowser.fileCount].isDirectory = false;
        fileBrowser.fileCount++;
    } else {
        // Scan the root directory
        FileBrowser_ScanDirectory("0:/");
    }
    
    // Mark as needing refresh
    fileBrowser.refreshNeeded = true;
}

// Handle user input for file browser
static void FileBrowser_HandleInput(void) {
    static float previousDial = -1.0f;
    static float dialAccumulator = 0.0f;
    static uint32_t lastDialTime = 0;
    uint32_t currentTime = HAL_GetTick();
    uint32_t buttons = INPUT_GetButtons();
    static uint32_t lastButtons = 0;
    uint32_t newPresses = buttons & ~lastButtons;
    float dial = INPUT_GetDial();
    
    // Initialize previousDial if needed
    if (previousDial < 0) {
        previousDial = dial;
    }
    
    // Calculate the delta, handling the wrap-around at 360 degrees
    float dialDelta = dial - previousDial;
    
    // Handle wrap-around (crossing from 0 to 360 or vice versa)
    if (dialDelta > 180.0f) {
        dialDelta -= 360.0f;
    } else if (dialDelta < -180.0f) {
        dialDelta += 360.0f;
    }
    
    // Add to accumulator
    dialAccumulator += dialDelta;
    
    // Process accumulated movement if threshold reached and debounce time passed
    if ((fabs(dialAccumulator) >= DIAL_THRESHOLD) && 
        (currentTime - lastDialTime >= DIAL_DEBOUNCE_TIME)) {
        
        if (dialAccumulator < 0) {
            // Move selection down (clockwise rotation)
            if (fileBrowser.selectedIndex < fileBrowser.fileCount - 1) {
                fileBrowser.selectedIndex++;
                
                // Adjust scroll if needed to show selected item
                if (fileBrowser.selectedIndex >= fileBrowser.scrollOffset + MAX_FILES_PER_PAGE) {
                    fileBrowser.scrollOffset = fileBrowser.selectedIndex - MAX_FILES_PER_PAGE + 1;
                }
            }
        } else {
            // Move selection up (counterclockwise rotation)
            if (fileBrowser.selectedIndex > 0) {
                fileBrowser.selectedIndex--;
                
                // Adjust scroll if needed to show selected item
                if (fileBrowser.selectedIndex < fileBrowser.scrollOffset) {
                    fileBrowser.scrollOffset = fileBrowser.selectedIndex;
                }
            }
        }
        
        // Reset accumulator and update time
        dialAccumulator = 0.0f;
        lastDialTime = currentTime;
        
        // Mark as needing refresh
        fileBrowser.refreshNeeded = true;
    }
    
    // Handle button presses
    if (newPresses & BUTTON_DIAL) {
        // Enter directory or display file info
        if (fileBrowser.selectedIndex < fileBrowser.fileCount) {
            FileEntry* selectedFile = &fileBrowser.files[fileBrowser.selectedIndex];
            
            if (selectedFile->isDirectory) {
                // Handle ".." (parent directory)
                if (strcmp(selectedFile->filename, "..") == 0) {
                    // Go up one directory by finding the last slash
                    char* lastSlash = strrchr(fileBrowser.currentPath, '/');
                    if (lastSlash != NULL && lastSlash != fileBrowser.currentPath && 
                        !(lastSlash == fileBrowser.currentPath + 2 && fileBrowser.currentPath[0] == '0' && fileBrowser.currentPath[1] == ':')) {
                        // Remove everything after the last slash
                        *lastSlash = '\0';
                    } else {
                        // If no slash found or we're at root level, go to root
                        strcpy(fileBrowser.currentPath, "0:/");
                    }
                } else {
                    // Enter subdirectory
                    // Add a slash if needed (except for root directory)
                    if (strcmp(fileBrowser.currentPath, "0:/") != 0 && 
                        fileBrowser.currentPath[strlen(fileBrowser.currentPath) - 1] != '/') {
                        strncat(fileBrowser.currentPath, "/", MAX_PATH_LEN - strlen(fileBrowser.currentPath) - 1);
                    }
                    
                    // Add the directory name
                    strncat(fileBrowser.currentPath, selectedFile->filename, 
                            MAX_PATH_LEN - strlen(fileBrowser.currentPath) - 1);
                }
                
                // Scan the new directory
                FileBrowser_ScanDirectory(fileBrowser.currentPath);
                
                // Mark as needing refresh
                fileBrowser.refreshNeeded = true;
            }
            // If it's a file, we could add file operations here
        }
    }
    
    // Keep track of previous dial position and buttons
    previousDial = dial;
    lastButtons = buttons;
}


// Hardware initialization functions

static void DMA_Init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();

    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

static void FMC_Init(void)
{
    FMC_NORSRAM_TimingTypeDef timing;

    fmcHandle.Instance = FMC_Bank1_R;
    fmcHandle.Extended = FMC_Bank1E_R;

    fmcHandle.Init.WaitSignalActive   = 0;
    fmcHandle.Init.WriteOperation     = FMC_WRITE_OPERATION_ENABLE;
    fmcHandle.Init.NSBank             = FMC_NORSRAM_BANK1;
    fmcHandle.Init.MemoryDataWidth    = FMC_NORSRAM_MEM_BUS_WIDTH_16;
    fmcHandle.Init.BurstAccessMode    = FMC_BURST_ACCESS_MODE_DISABLE;
    fmcHandle.Init.DataAddressMux     = FMC_DATA_ADDRESS_MUX_DISABLE;
    fmcHandle.Init.MemoryType         = FMC_MEMORY_TYPE_SRAM;
    fmcHandle.Init.WaitSignalPolarity = FMC_WAIT_SIGNAL_POLARITY_LOW;

    timing.BusTurnAroundDuration = 0;
    timing.CLKDivision           = 1;
    timing.DataLatency           = 0;
    timing.AccessMode            = 0;
    timing.DataSetupTime         = 2;
    timing.AddressSetupTime      = 2;   
    timing.AddressHoldTime       = 0;
    if (HAL_SRAM_Init(&fmcHandle, &timing, NULL) != HAL_OK) {
        while (1)
            ;
    }

    HAL_SetFMCMemorySwappingConfig(FMC_SWAPBMAP_SDRAM_SRAM);
}

static void TIMx_PWM_MspInit(TIM_HandleTypeDef *handle)
{
    GPIO_InitTypeDef gpioConfig = { 0 };

    if (handle->Instance == TIM3) {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();

        gpioConfig.Alternate = GPIO_AF2_TIM3;
        gpioConfig.Pull = GPIO_NOPULL;
        gpioConfig.Speed = GPIO_SPEED_FREQ_LOW;
        gpioConfig.Pin = GPIO_PIN_1;
        gpioConfig.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOB, &gpioConfig);

        gpioConfig.Pin = GPIO_PIN_8;
        gpioConfig.Alternate = GPIO_AF2_TIM3;
        gpioConfig.Speed = GPIO_SPEED_FREQ_LOW;
        gpioConfig.Pull = GPIO_NOPULL;
        gpioConfig.Mode = GPIO_MODE_AF_PP;
        HAL_GPIO_Init(GPIOC, &gpioConfig);
    }
}

static void TIM3_Init(void)
{
    TIM_MasterConfigTypeDef masterConfig = { 0 };
    TIM_OC_InitTypeDef channelConfig = { 0 };

    tim3Handle.Instance = TIM3;
    tim3Handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    tim3Handle.Init.Prescaler         = 0;
    tim3Handle.Init.CounterMode       = TIM_COUNTERMODE_UP;
    tim3Handle.Init.Period            = 0xffff;
    tim3Handle.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    if (HAL_TIM_PWM_Init(&tim3Handle) != HAL_OK) {
        while (1)
            ;
    }

    masterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    masterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    if (HAL_TIMEx_MasterConfigSynchronization(&tim3Handle, &masterConfig) != HAL_OK) {
        while (1)
            ;
    }

    channelConfig.OCFastMode = TIM_OCFAST_DISABLE;
    channelConfig.Pulse      = 0;
    channelConfig.OCPolarity = TIM_OCPOLARITY_HIGH;
    channelConfig.OCMode     = TIM_OCMODE_PWM1;
    if (HAL_TIM_PWM_ConfigChannel(&tim3Handle, &channelConfig, TIM_CHANNEL_3) != HAL_OK) {
        while (1)
            ;
    }

    if (HAL_TIM_PWM_ConfigChannel(&tim3Handle, &channelConfig, TIM_CHANNEL_4) != HAL_OK) {
        while (1)
            ;
    }

    TIMx_PWM_MspInit(&tim3Handle);
}

static void MDMA_Init(void)
{
    __HAL_RCC_MDMA_CLK_ENABLE();

    mdmaHandle.Instance = MDMA_Channel0;

    mdmaHandle.Init.BufferTransferLength     = 0x80;
    mdmaHandle.Init.DataAlignment            = MDMA_DATAALIGN_PACKENABLE;
    mdmaHandle.Init.Request                  = MDMA_REQUEST_SW;
    mdmaHandle.Init.DestinationInc           = MDMA_DEST_INC_DISABLE;
    mdmaHandle.Init.SourceDataSize           = MDMA_SRC_DATASIZE_HALFWORD;
    mdmaHandle.Init.DestDataSize             = MDMA_DEST_DATASIZE_HALFWORD;
    mdmaHandle.Init.TransferTriggerMode      = MDMA_BLOCK_TRANSFER;
    mdmaHandle.Init.Priority                 = MDMA_PRIORITY_VERY_HIGH;
    mdmaHandle.Init.Endianness               = MDMA_LITTLE_BYTE_ENDIANNESS_EXCHANGE;
    mdmaHandle.Init.SourceInc                = MDMA_SRC_INC_HALFWORD;
    mdmaHandle.Init.SourceBurst              = MDMA_SOURCE_BURST_SINGLE;
    mdmaHandle.Init.DestBurst                = MDMA_DEST_BURST_SINGLE;
    mdmaHandle.Init.SourceBlockAddressOffset = 0x0;
    mdmaHandle.Init.DestBlockAddressOffset   = 0x0;

    if (HAL_MDMA_Init(&mdmaHandle) != HAL_OK) {
        while (1)
            ;
    }
}

static void ADC_Init(void)
{
    RCC_PeriphCLKInitTypeDef RCC_PeriphCLKInitStruct = { 0 };
    RCC_PeriphCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    RCC_PeriphCLKInitStruct.AdcClockSelection    = RCC_ADCCLKSOURCE_PLL2;
    HAL_RCCEx_PeriphCLKConfig(&RCC_PeriphCLKInitStruct);

    adcHandle.Instance = ADC1;

    adcHandle.Init.Overrun                            = ADC_OVR_DATA_OVERWRITTEN;
    adcHandle.Init.LeftBitShift                       = ADC_LEFTBITSHIFT_NONE;
    adcHandle.Init.OversamplingMode                   = ENABLE;
    adcHandle.Init.Oversampling.Ratio                 = 0x10;
    adcHandle.Init.Oversampling.RightBitShift         = ADC_RIGHTBITSHIFT_4;
    adcHandle.Init.Oversampling.TriggeredMode         = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    adcHandle.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
    adcHandle.Init.ExternalTrigConv                   = ADC_SOFTWARE_START;
    adcHandle.Init.ExternalTrigConvEdge               = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adcHandle.Init.ConversionDataManagement           = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    adcHandle.Init.Resolution                         = ADC_RESOLUTION_16B;
    adcHandle.Init.ScanConvMode                       = ADC_SCAN_ENABLE;
    adcHandle.Init.EOCSelection                       = ADC_EOC_SINGLE_CONV;
    adcHandle.Init.LowPowerAutoWait                   = DISABLE;
    adcHandle.Init.ContinuousConvMode                 = ENABLE;
    adcHandle.Init.NbrOfConversion                    = 0x3;
    adcHandle.Init.DiscontinuousConvMode              = DISABLE;
    adcHandle.Init.ClockPrescaler                     = ADC_CLOCK_ASYNC_DIV8;
    if (HAL_ADC_Init(&adcHandle) != HAL_OK) {
        while (1)
            ;
    }

    ADC_MultiModeTypeDef multiMode = { 0 };
    multiMode.Mode = ADC_MODE_INDEPENDENT;
    if (HAL_ADCEx_MultiModeConfigChannel(&adcHandle, &multiMode) != HAL_OK) {
        while (1)
            ;
    }

    ADC_ChannelConfTypeDef channelConf = { 0 };
    channelConf.OffsetSignedSaturation = DISABLE;
    channelConf.Offset                 = 0x0;
    channelConf.OffsetNumber           = ADC_OFFSET_NONE;
    channelConf.SingleDiff             = ADC_SINGLE_ENDED;
    channelConf.SamplingTime           = ADC_SAMPLETIME_64CYCLES_5;
    channelConf.Rank                   = ADC_REGULAR_RANK_1;
    channelConf.Channel                = ADC_CHANNEL_4;
    if (HAL_ADC_ConfigChannel(&adcHandle, &channelConf) != HAL_OK) {
        while (1)
            ;
    }

    channelConf.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&adcHandle, &channelConf) != HAL_OK) {
        while (1)
            ;
    }

    channelConf.Rank    = ADC_REGULAR_RANK_3;
    channelConf.Channel = ADC_CHANNEL_10;
    if (HAL_ADC_ConfigChannel(&adcHandle, &channelConf) != HAL_OK) {
        while (1)
            ;
    }

    HAL_ADCEx_Calibration_Start(&adcHandle, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);

    adc2Handle.Instance = ADC2;

    adc2Handle.Init.Oversampling.RightBitShift         = ADC_RIGHTBITSHIFT_4;
    adc2Handle.Init.Oversampling.TriggeredMode         = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
    adc2Handle.Init.ConversionDataManagement           = ADC_CONVERSIONDATA_DMA_CIRCULAR;
    adc2Handle.Init.Overrun                            = ADC_OVR_DATA_OVERWRITTEN;
    adc2Handle.Init.LeftBitShift                       = ADC_LEFTBITSHIFT_NONE;
    adc2Handle.Init.OversamplingMode                   = ENABLE;
    adc2Handle.Init.Oversampling.Ratio                 = 0x10;
    adc2Handle.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
    adc2Handle.Init.ExternalTrigConv                   = ADC_SOFTWARE_START;
    adc2Handle.Init.ExternalTrigConvEdge               = ADC_EXTERNALTRIGCONVEDGE_NONE;
    adc2Handle.Init.ScanConvMode                       = ADC_SCAN_ENABLE;
    adc2Handle.Init.EOCSelection                       = ADC_EOC_SINGLE_CONV;
    adc2Handle.Init.LowPowerAutoWait                   = DISABLE;
    adc2Handle.Init.ContinuousConvMode                 = ENABLE;
    adc2Handle.Init.Resolution                         = ADC_RESOLUTION_16B;
    adc2Handle.Init.NbrOfConversion                    = 0x2;
    adc2Handle.Init.DiscontinuousConvMode              = DISABLE;
    adc2Handle.Init.ClockPrescaler                     = ADC_CLOCK_ASYNC_DIV8;
    if (HAL_ADC_Init(&adc2Handle) != HAL_OK) {
        while (1)
            ;
    }

    channelConf.SingleDiff              = ADC_SINGLE_ENDED;
    channelConf.OffsetSignedSaturation  = DISABLE;
    channelConf.OffsetNumber            = ADC_OFFSET_NONE;
    channelConf.Offset                  = 0x0;
    channelConf.SamplingTime            = ADC_SAMPLETIME_64CYCLES_5;
    channelConf.Rank                    = ADC_REGULAR_RANK_1;
    channelConf.Channel                 = ADC_CHANNEL_9;
    if (HAL_ADC_ConfigChannel(&adc2Handle, &channelConf) != HAL_OK) {
        while (1)
            ;
    }

    channelConf.Rank = ADC_REGULAR_RANK_2;
    if (HAL_ADC_ConfigChannel(&adc2Handle, &channelConf) != HAL_OK) {
        while (1)
            ;
    }

    HAL_ADCEx_Calibration_Start(&adc2Handle, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
}