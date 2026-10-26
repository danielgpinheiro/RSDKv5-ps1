/*
 * Mania's user files on the memory card (docs/ports/mania/32 phase 8): the card side. The engine (RSDK/User/Core/UserStorage.cpp,
 * PS1 branch of LoadUserFile / SaveUserFile) keeps the sections and the packing and stages the payload in its own
 * memory; this file only names the card file and moves the bytes (ps1/memcard's in-place calls: no heap copies, the
 * heap is STG's at 2 MB). Blocking, called from the game's updates (no loading bracket: PS1LoadingBegin suspends the
 * chain arenas, which only a frame start may do); psyqo pumps GPU callbacks meanwhile.
 *
 * Card file: "BAHBRW-00006MANIA" in slot 1 (region A, the ports' homebrew product numbers: 3 Sonic CD, 4 Sonic 2,
 * 5 Sonic 1, 7 TMC), title "SONIC MANIA", icon PS1/Memcard icons/mania-mc-icon.png (memcard_icons.h). Never formats.
 */
#include <stdint.h>

#include "memcard.hh"
#include "memcard_icons.h"
#include <psyqo/gpu.hh>

extern psyqo::GPU *g_ps1GPU;

namespace {
constexpr uint16_t VERSION = 1;
const char *const TITLE    = "SONIC MANIA";
char s_file[21];
bool s_named = false;

const char *FileName() {
    if (!s_named)
        s_named = PS1McFileName(s_file, 'A', "HBRW-00006", "MANIA");
    return s_file;
}
} // namespace

extern "C" {
volatile uint32_t g_ps1CardLastError = 0; // the last psyqo MemoryCard::Error a card call returned (GDB)
volatile uint32_t g_ps1CardWriteUs = 0, g_ps1CardReadUs = 0; // the last write / read: how long the game stood still

// The card file's payload room (bytes, ps1/memcard's header included) from its block count: its blocks less the title
// frame and the icon's frames. 0: no file (or no card: the error in g_ps1CardLastError).
uint32_t PS1UserCardRoom() {
    psyqo::MemoryCardFileSystem::FileEntry list[15];
    uint32_t count = 0;
    PS1McError e   = PS1McList(psyqo::MemoryCard::Port::Port0, list, 15, &count);
    if (e != PS1McError::OK) {
        g_ps1CardLastError = (uint32_t)e;
        return 0;
    }
    const char *name = FileName();
    for (uint32_t i = 0; i < count; ++i) {
        const char *a = list[i].name, *b = name;
        while (*a && *a == *b) ++a, ++b;
        if (*a == *b)
            return list[i].sizeInBlocks * 8192u - 128u * (1u + s_ps1McIconMania.frameCount);
    }
    g_ps1CardLastError = (uint32_t)PS1McError::FileNotFound;
    return 0;
}

// docs/ports/mania/32 9f: the card's free blocks (15 less the files' blocks) and whether Mania's file is there; 0xFFFFFFFF: no
// card / unreadable (the error in g_ps1CardLastError)
uint32_t PS1UserCardFreeBlocks(uint32_t *hasFile) {
    psyqo::MemoryCardFileSystem::FileEntry list[15];
    uint32_t count = 0;
    *hasFile       = 0;
    PS1McError e   = PS1McList(psyqo::MemoryCard::Port::Port0, list, 15, &count);
    if (e != PS1McError::OK) {
        g_ps1CardLastError = (uint32_t)e;
        return 0xFFFFFFFFu;
    }
    const char *name = FileName();
    uint32_t used    = 0;
    for (uint32_t i = 0; i < count; ++i) {
        used += list[i].sizeInBlocks;
        const char *a = list[i].name, *b = name;
        while (*a && *a == *b) ++a, ++b;
        if (*a == *b)
            *hasFile = 1;
    }
    return used >= 15 ? 0 : 15 - used;
}

// The payload into buf (cap bytes: the room above); the data at buf + PS1_MC_HEADER_SIZE, *outLen bytes. 0 = OK, else
// the psyqo error (FileNotFound, BadData, ...)
uint32_t PS1UserCardRead(uint8_t *buf, uint32_t cap, uint32_t *outLen) {
    uint32_t t0  = g_ps1GPU ? g_ps1GPU->now() : 0;
    PS1McError e = PS1McLoadInPlace(psyqo::MemoryCard::Port::Port0, FileName(), buf, cap, outLen, nullptr);
    g_ps1CardReadUs = g_ps1GPU ? g_ps1GPU->now() - t0 : 0;
    if (e != PS1McError::OK)
        g_ps1CardLastError = (uint32_t)e;
    return (uint32_t)e;
}

// buf: PS1_MC_HEADER_SIZE bytes of room, then len bytes of payload. 0 = OK
uint32_t PS1UserCardWrite(uint8_t *buf, uint32_t len) {
    uint32_t t0  = g_ps1GPU ? g_ps1GPU->now() : 0;
    PS1McError e = PS1McSaveInPlace(psyqo::MemoryCard::Port::Port0, FileName(), TITLE, s_ps1McIconMania, buf, len, VERSION);
    g_ps1CardWriteUs = g_ps1GPU ? g_ps1GPU->now() - t0 : 0;
    if (e != PS1McError::OK)
        g_ps1CardLastError = (uint32_t)e;
    return (uint32_t)e;
}

// In the background (2.5 s a write in PCSX-Redux: the game runs on): buf (header room + payload) must stay untouched until
// it ends (the engine's resident copy: it waits with PS1UserCardWait before changing it). 0 = started
volatile uint32_t g_ps1CardAsyncDone = 0, g_ps1CardAsyncErrors = 0;
static uint32_t s_asyncT0 = 0;
uint32_t PS1UserCardWriteAsync(uint8_t *buf, uint32_t len) {
    s_asyncT0    = g_ps1GPU ? g_ps1GPU->now() : 0;
    PS1McError e = PS1McSaveInPlaceAsync(psyqo::MemoryCard::Port::Port0, FileName(), TITLE, s_ps1McIconMania, buf, len, VERSION,
                                         [](PS1McError r) {
                                             g_ps1CardWriteUs = g_ps1GPU ? g_ps1GPU->now() - s_asyncT0 : 0;
                                             if (r == PS1McError::OK) {
                                                 g_ps1CardAsyncDone = g_ps1CardAsyncDone + 1;
                                             }
                                             else {
                                                 g_ps1CardAsyncErrors = g_ps1CardAsyncErrors + 1;
                                                 g_ps1CardLastError   = (uint32_t)r;
                                             }
                                         });
    if (e != PS1McError::OK)
        g_ps1CardLastError = (uint32_t)e;
    return (uint32_t)e;
}

uint32_t PS1UserCardBusy() { return PS1McBusy(); }
void PS1UserCardWait() { PS1McWaitIdle(); }
}
