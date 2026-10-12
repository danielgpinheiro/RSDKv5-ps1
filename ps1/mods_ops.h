// docs/32 phase 13: what the resident asks of the Mods overlay (ps1/mods/mods.cpp, linked at 0x80200000 and loaded only
// on a console with 8 MB or more: ps1/hwdetect.cpp). NULL on a stock console: every caller checks it first.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    // M2: the CD sector cache in the RAM above 2 MB. get: bytes of `sector` (from byte `off`) copied to dst, up to want,
    // as far as the cache holds them in a row (0: a miss); put: `count` sectors read from the CD at `lba`
    uint32_t (*fsGet)(uint32_t sector, uint32_t off, uint8_t *dst, uint32_t want);
    void (*fsPut)(uint32_t lba, uint32_t count, const uint8_t *src);
} PS1ModsOps;
extern const PS1ModsOps *volatile g_ps1Mods;
#ifdef __cplusplus
}
#endif
