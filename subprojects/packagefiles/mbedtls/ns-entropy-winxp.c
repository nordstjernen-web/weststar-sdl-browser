/* Mbed TLS's entropy source for Windows XP, which lacks BCryptGenRandom:
 * RtlGenRandom, the system's generator, exported by advapi32.dll as
 * SystemFunction036 since Windows XP. Built only where _WIN32_WINNT is
 * below Vista's, with MBEDTLS_ENTROPY_HARDWARE_ALT (see meson.build). */
#include <windows.h>
#include <ntsecapi.h>
#include <stddef.h>

#include "entropy_poll.h"

int
mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                      size_t *olen)
{
    (void)data;
    *olen = 0;
    while (len > 0) {
        ULONG n = len > 0x10000 ? 0x10000 : (ULONG)len;
        if (!RtlGenRandom(output, n))
            return -1;
        output += n;
        len -= n;
        *olen += n;
    }
    return 0;
}
