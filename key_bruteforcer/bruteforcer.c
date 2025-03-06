/**
 * Nintendo Alarmo content key bruteforcer.
 * Created in 2024 by GaryOderNichts.
 * 
 * Compiled with `gcc bruteforcer.c -maes -O3 -o bruteforcer`
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <wmmintrin.h>

typedef struct {
    uint8_t iv[16];
    uint8_t encrypted_parts[4][16];
} aes_data_t;

static void aes128_encrypt(const uint8_t *key, const uint8_t *plaintext, uint8_t *ciphertext);
static int hex2bin(const char *str, void *bytes, size_t maxsize);

int main(int argc, char const *argv[])
{
    if (argc != 2) {
        printf("Usage: %s <aes_data.bin or hex string>\n", argv[0]);
        return 1;
    }

    // Try to read in AES data from file
    aes_data_t data;
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        if (hex2bin(argv[1], &data, sizeof(data)) != sizeof(data)) {
            printf("Failed to open file or invalid hex string\n");
            return 1;
        }
    } else {
        if (fread(&data, 1, sizeof(data), f) != sizeof(data)) {
            fclose(f);
            printf("Failed to read file\n");
            return 1;
        }

        fclose(f);
    }

    // Save start time
    time_t start_time = time(NULL);

    const uint8_t *iv = &data.iv[0];
    uint8_t full_key[16] = { 0 };

    for (int i = 3; i >= 0; i--) {
        // Start with the least blanked out key data
        const uint8_t *expected = &data.encrypted_parts[i][0];

        // Start guessing
        uint32_t key = 0;
        while (1) {
            // Fill in the key
            full_key[(i * 4) + 0] = (uint8_t) (key >> 24);
            full_key[(i * 4) + 1] = (uint8_t) (key >> 16);
            full_key[(i * 4) + 2] = (uint8_t) (key >> 8);
            full_key[(i * 4) + 3] = (uint8_t) (key);

            // Encrypting the IV should result in the expected data
            uint8_t result[16];
            aes128_encrypt(full_key, iv, result);

            if (memcmp(result, expected, 16) == 0) {
                // Found key
                printf("\nFound key part %d / 4: %08x\n", (4 - i), key);
                break;
            }

            // Make sure we don't overflow
            if (key == ~0) {
                printf("\nCouldn't find match\n");
                return 1;
            }

            // Update progress
            if ((key & 0xFFFF) == 0) {
                printf("\rBruteforcing key part %d / 4... %.1f%% (0x%08x / 0xffffffff)", (4 - i), (key / 4294967296.0) * 100.0f, key);
                fflush(stdout);
            }

            // Increment test key
            key++;
        }
    }

    // Print output
    printf("Found key! Took %ld seconds.\n", time(NULL) - start_time);
    printf("Key:\t");
    for (int i = 0; i < 16; i++) {
        printf("%02x", full_key[i]);
    }
    printf("\n");
    printf("IV:\t");
    for (int i = 0; i < 16; i++) {
        printf("%02x", iv[i]);
    }
    printf("\n");

    return 0;
}

static inline int char2bin(char c)
{
    if((c >= 'a') && (c <= 'f'))
        return c - 'a' + 10;
    if((c >= '0') && (c <= '9'))
        return c - '0';
    if((c >= 'A') && (c <= 'F'))
        return c - 'A' + 10;

    return -1;
}

static int hex2bin(const char *str, void *bytes, size_t maxsize)
{
    uint8_t *bytes_ptr = (uint8_t *)bytes;
    size_t bytes_in = 0;

    // Only accept hex pairs
    size_t len = strlen(str);
    if ((len & 1) != 0 || (len / 2) > maxsize) {
        return -1;
    }

    for (; str[0] != '\0'; str += 2) {
        int char0 = char2bin(str[0]);
        int char1 = char2bin(str[1]);

        // Check for invalid characters
        if (char0 == -1 || char1 == -1) {
            return -1;
        }

        bytes_ptr[bytes_in++] = (char0 << 4) | char1;
    }

    return bytes_in;
}

// From https://stackoverflow.com/a/32313659/11511475
#define AES_128_key_exp(k, rcon) aes128_key_expansion(k, _mm_aeskeygenassist_si128(k, rcon))
static __m128i aes128_key_expansion(__m128i key, __m128i keygened)
{
    keygened = _mm_shuffle_epi32(keygened, _MM_SHUFFLE(3,3,3,3));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    key = _mm_xor_si128(key, _mm_slli_si128(key, 4));
    return _mm_xor_si128(key, keygened);
}

static void aes128_encrypt(const uint8_t *key, const uint8_t *plaintext, uint8_t *ciphertext)
{
    // Start by loading the key
    __m128i aes_key_schedule[11];
    aes_key_schedule[0] = _mm_loadu_si128((const __m128i*) key);
    aes_key_schedule[1] = AES_128_key_exp(aes_key_schedule[0], 0x01);
    aes_key_schedule[2] = AES_128_key_exp(aes_key_schedule[1], 0x02);
    aes_key_schedule[3] = AES_128_key_exp(aes_key_schedule[2], 0x04);
    aes_key_schedule[4] = AES_128_key_exp(aes_key_schedule[3], 0x08);
    aes_key_schedule[5] = AES_128_key_exp(aes_key_schedule[4], 0x10);
    aes_key_schedule[6] = AES_128_key_exp(aes_key_schedule[5], 0x20);
    aes_key_schedule[7] = AES_128_key_exp(aes_key_schedule[6], 0x40);
    aes_key_schedule[8] = AES_128_key_exp(aes_key_schedule[7], 0x80);
    aes_key_schedule[9] = AES_128_key_exp(aes_key_schedule[8], 0x1B);
    aes_key_schedule[10] = AES_128_key_exp(aes_key_schedule[9], 0x36);

    // Load plaintext
    __m128i m = _mm_loadu_si128((const __m128i *) plaintext);

    // Encrypt block
    m = _mm_xor_si128(m, aes_key_schedule[0]);
    m = _mm_aesenc_si128(m, aes_key_schedule[1]);
    m = _mm_aesenc_si128(m, aes_key_schedule[2]);
    m = _mm_aesenc_si128(m, aes_key_schedule[3]);
    m = _mm_aesenc_si128(m, aes_key_schedule[4]);
    m = _mm_aesenc_si128(m, aes_key_schedule[5]);
    m = _mm_aesenc_si128(m, aes_key_schedule[6]);
    m = _mm_aesenc_si128(m, aes_key_schedule[7]);
    m = _mm_aesenc_si128(m, aes_key_schedule[8]);
    m = _mm_aesenc_si128(m, aes_key_schedule[9]);
    m = _mm_aesenclast_si128(m, aes_key_schedule[10]);

    // Store cyphertext
    _mm_storeu_si128((__m128i *) ciphertext, m);
}