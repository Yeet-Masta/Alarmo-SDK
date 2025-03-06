/**
 * Nintendo Alarmo content key bruteforcer.
 * Created in 2024 by GaryOderNichts.
 */
#include <stdint.h>

// Make sure to set this according to the firmware version the alarmo is on
// 1.0.0 -> 100
// 2.0.0 -> 200
#define FIRMWARE_VERSION 100

// Function pointers to the 1.0.0 firmware loaded in external RAM
#if FIRMWARE_VERSION == 100
#define CRYP_AES_CTR_Decrypt ((void (*)(uint32_t *counter, void *outData, uint32_t outSize, const void *inData, uint32_t inSize))(0x700724c8 | 1))
#define CRYP_AES_CTR_Encrypt ((void (*)(uint32_t *counter, void *outData, uint32_t outSize, const void *inData, uint32_t inSize))(0x7007245c | 1))
#elif FIRMWARE_VERSION == 200
#define CRYP_AES_CTR_Decrypt ((void (*)(uint32_t *counter, void *outData, uint32_t outSize, const void *inData, uint32_t inSize))(0x7007798c | 1))
#define CRYP_AES_CTR_Encrypt ((void (*)(uint32_t *counter, void *outData, uint32_t outSize, const void *inData, uint32_t inSize))(0x70077920 | 1))
#else
#error Unsupported firmware version
#endif

#define CRYP_BASE (0x48021000UL)
#define CRYP_KEY_BASE ((volatile uint32_t *)(CRYP_BASE + 0x20UL))
#define CRYP_IV_BASE ((volatile uint32_t *)(CRYP_BASE + 0x40UL))

static int memcmp32(const uint32_t *a, const uint32_t *b, uint32_t count)
{
    while (count-- > 0) {
        if (*a++ != *b++) {
            return a[-1] < b[-1] ? -1 : 1;
        }
    }

    return 0;
}

int main(void)
{
    // Disable interrupts
    __asm__ __volatile__ ("cpsid i" : : : "memory");

    // Pointer to AXI SRAM where results are stored
    uint32_t *result_ptr = (uint32_t*) 0x24000000;

    // Buffer to store expected and actual data in
    uint32_t expected[4];
    uint32_t actual[4];

    // Clear zero buffer and results
    uint32_t zeroes[4];
    for (int i = 0; i < 4; i++) {
        zeroes[i] = 0;

        // Initialize results with Fs
        result_ptr[i] = ~0;
        result_ptr[4 + i] = ~0;
    }

    // store IV
    result_ptr[4] = CRYP_IV_BASE[0];
    result_ptr[5] = CRYP_IV_BASE[1];
    result_ptr[6] = CRYP_IV_BASE[2];
    result_ptr[7] = CRYP_IV_BASE[3];

    // To keep track of  progress create a progress counter
    result_ptr[8] = ~0;

    // Encrypt zeroes with the unmodified key, to create the expected value
    uint32_t counter = 0;
    CRYP_AES_CTR_Encrypt(&counter, expected, 16, zeroes, 16);

    // Start offsets for the key
    // Can be filled out if the process was cancelled and should be resumed while already having made progress 
    uint32_t start_offsets[4] = { 0, 0, 0, 0 };

    for (int i = 0; i < 4; i++) {
        uint32_t key = start_offsets[i];
        while (1) {
            // Update partial key
            CRYP_KEY_BASE[4 + i] = key;

            // Encrypt with partially updated key
            counter = 0;
            CRYP_AES_CTR_Encrypt(&counter, actual, 16, zeroes, 16);

            // Check if output matches
            if (memcmp32(actual, expected, 4) == 0) {
                // Found a match, store key fragment
                result_ptr[i] = key;
                break;
            }

            if (key == ~0) {
                // Could not find a match, this shouldn't happen
                __asm__ __volatile__("bkpt");
                break;
            }

            // Store progress
            if ((key & 0xfff) == 0) {
                result_ptr[8] = key;
            }

            // Increment key
            key++;
        }
    }

    // All done
    __asm__ __volatile__("bkpt");
    while (1)
        ;
}
