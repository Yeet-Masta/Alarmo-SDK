/**
 * Nintendo Alarmo content key bruteforce data dumper.
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

int main(void)
{
    // Disable interrupts
    __asm__ __volatile__ ("cpsid i" : : : "memory");

    // Pointer to AXI SRAM where results are stored
    uint32_t *result_ptr = (uint32_t*) 0x24000000;

    // Buffer to hold encrypted data
    uint32_t encrypted[4];

    // Clear zero buffer
    uint32_t zeroes[4];
    for (int i = 0; i < 4; i++) {
        zeroes[i] = 0;
    }

    // store IV
    *result_ptr++ = __builtin_bswap32(CRYP_IV_BASE[0]);
    *result_ptr++ = __builtin_bswap32(CRYP_IV_BASE[1]);
    *result_ptr++ = __builtin_bswap32(CRYP_IV_BASE[2]);
    *result_ptr++ = 0; // for the counter just store 0

    for (int i = 0; i < 4; i++) {
        // Start blanking out parts of the key after the first round
        if (i > 0) {
            CRYP_KEY_BASE[4 + (i - 1)] = 0;
        }

        // Encrypt zeroes
        uint32_t counter = 0;
        CRYP_AES_CTR_Encrypt(&counter, encrypted, sizeof(encrypted), zeroes, sizeof(zeroes));

        // Store encrypted data
        *result_ptr++ = (encrypted[0]);
        *result_ptr++ = (encrypted[1]);
        *result_ptr++ = (encrypted[2]);
        *result_ptr++ = (encrypted[3]);
    }

    // All done
    __asm__ __volatile__("bkpt");
    while (1)
        ;
}
