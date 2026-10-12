// docs/32 phase 13 M2: the Mods overlay. Linked at 0x80200000 (tools/gen_overlays.py modlink) and loaded by the Logos
// overlay's probes (ps1/hwdetect.cpp) only on a console with 8 or 16 MB of RAM: none of this is resident, a stock
// console never reads the file. It owns the RAM above 2 MB: the overlay itself, then an arena to the top of bank 0
// (8 MB) and, with the 16 MB mods, bank 1 (0x80800000-0x80FFFFFF; CPU and DMA data only: GPU linked lists end at
// bit 23).
//
// The CD sector cache (M2): every sector the file system reads from the CD (ps1/cdrom_fs.cpp: a window fill, a direct
// read) is kept here in 32 KB chunks of 16 sectors, the least recently used chunk making room. A read the cache holds
// costs no CD request, so a scene loaded again (a death, a retry), the files acts share and a sprite group swapped in
// again (Super Sonic, a boss, the act clear: ps1/../PS1Atlas.cpp) come from RAM, and the XA music only stops for a read
// that really goes to the CD. The disc never changes: nothing is ever invalidated.
#include <stdint.h>
#include <string.h>

#include "../mods_ops.h"

extern "C" {
extern volatile uint32_t g_ps1RamMB;
extern char __ps1_ovl_end[];
volatile uint32_t g_ps1ModsChunks = 0, g_ps1ModsHits = 0, g_ps1ModsHitBytes = 0, g_ps1ModsPuts = 0, g_ps1ModsEvictions = 0;
}

namespace {
constexpr uint32_t SECTOR = 2048, CHUNK_SECTORS = 16, CHUNK = SECTOR * CHUNK_SECTORS;
constexpr uint32_t MAX_CHUNKS = 512; // 16 MB of chunks at most (8 + 8 MB banks)
struct Chunk {
    uint32_t key; // first sector / 16, + 1 (0: free)
    uint32_t age;
    uint16_t mask; // sectors present
    uint8_t *data;
};
Chunk s_chunk[MAX_CHUNKS];
uint32_t s_count = 0, s_used = 0, s_clock = 0; // chunks in all / holding a key (always [0, s_used): filled in order)
// A read finds its chunk without a scan: the last chunk found (a file is read on in order, many small reads a sector),
// else a hint table indexed by the key's low bits (checked: a chunk may have been given another key since)
Chunk *s_last = nullptr;
uint16_t s_hint[256];

Chunk *find(uint32_t key) {
    if (s_last && s_last->key == key)
        return s_last;
    Chunk *h = &s_chunk[s_hint[key & 255]];
    if (h->key == key)
        return s_last = h;
    for (uint32_t i = 0; i < s_used; i++)
        if (s_chunk[i].key == key) {
            s_hint[key & 255] = (uint16_t)i;
            return s_last = &s_chunk[i];
        }
    return nullptr;
}

// the chunk for `key`: a free one while there are any, else the least recently used one emptied for it
Chunk *take(uint32_t key) {
    Chunk *c = find(key);
    if (c)
        return c;
    Chunk *v;
    if (s_used < s_count) {
        v = &s_chunk[s_used++];
    } else {
        v = &s_chunk[0];
        for (uint32_t i = 1; i < s_count; i++)
            if (s_chunk[i].age < v->age)
                v = &s_chunk[i];
        g_ps1ModsEvictions = g_ps1ModsEvictions + 1;
    }
    v->key                = key;
    v->mask               = 0;
    s_hint[key & 255]     = (uint16_t)(v - s_chunk);
    return s_last = v;
}

uint32_t fsGet(uint32_t sector, uint32_t off, uint8_t *dst, uint32_t want) {
    uint32_t got = 0;
    while (want > 0) {
        Chunk *c = find(sector / CHUNK_SECTORS + 1);
        uint32_t i = sector % CHUNK_SECTORS;
        if (!c || !(c->mask & (1u << i)))
            break;
        c->age     = ++s_clock;
        uint32_t n = SECTOR - off;
        if (n > want)
            n = want;
        memcpy(dst, c->data + i * SECTOR + off, n);
        dst += n, got += n, want -= n;
        off = 0, sector++;
    }
    if (got) {
        g_ps1ModsHits     = g_ps1ModsHits + 1;
        g_ps1ModsHitBytes = g_ps1ModsHitBytes + got;
    }
    return got;
}

void fsPut(uint32_t lba, uint32_t count, const uint8_t *src) {
    for (uint32_t k = 0; k < count; k++, src += SECTOR) {
        uint32_t s = lba + k, i = s % CHUNK_SECTORS;
        Chunk *c   = take(s / CHUNK_SECTORS + 1);
        c->age     = ++s_clock;
        if (c->mask & (1u << i))
            continue;
        memcpy(c->data + i * SECTOR, src, SECTOR);
        c->mask |= 1u << i;
    }
    g_ps1ModsPuts = g_ps1ModsPuts + 1;
}

const PS1ModsOps s_ops = {fsGet, fsPut};

void addChunks(uintptr_t from, uintptr_t to) {
    for (from = (from + CHUNK - 1) & ~(uintptr_t)(CHUNK - 1); from + CHUNK <= to && s_count < MAX_CHUNKS; from += CHUNK) {
        s_chunk[s_count].key  = 0;
        s_chunk[s_count].age  = 0;
        s_chunk[s_count].mask = 0;
        s_chunk[s_count].data = (uint8_t *)from;
        s_count++;
    }
}
} // namespace

extern "C" void PS1ModsRegister() {
    s_count = s_used = 0;
    s_last  = nullptr;
    addChunks((uintptr_t)__ps1_ovl_end, 0x80800000u);
    if (g_ps1RamMB >= 16)
        addChunks(0x80800000u, 0x81000000u);
    g_ps1ModsChunks = s_count;
    g_ps1Mods       = &s_ops;
}

// the header ps1/hwdetect.cpp checks (the code overlays' layout: tools/gen_overlays.py)
typedef struct {
    unsigned magic, size;
    void (*reg)(void);
    unsigned base;
} PS1OverlayHeader;
extern "C" char __ps1_ovl_start[], PS1_OVL_SIZE[];
__attribute__((section(".ovl_header"), used)) const PS1OverlayHeader ps1OvlHeader = {0x31444F4Du, (unsigned)PS1_OVL_SIZE, // 'MOD1'
                                                                                      PS1ModsRegister,
                                                                                      (unsigned)__ps1_ovl_start};
