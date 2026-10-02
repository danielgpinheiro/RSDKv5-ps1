/*
 * Memory card saves (phase 8): a thin layer over psyqo's MemoryCardFileSystem for the RSDK ports.
 *
 * Sonic Nexus (RSDKv2) has nothing to save and never calls this (the user's decision); it is here,
 * proven by the PS1_MEMCARD_TEST self-test (RSDKv2/main.cpp), for the v3+ ports. It is compiled
 * only for `make MEMCARD_TEST=1` (a port that saves adds ps1/memcard.cpp to SRCS).
 *
 * - Files use Sony names: "B" + region + 10-char product code + up to 8 chars (PS1McFileName).
 * - The payload starts with a 16-byte header: "RSDK", u16 version, u16 0, u32 length, u32 CRC-32
 *   of the data. psyqo checksums directory frames only, so loads check the CRC (BadData).
 * - Never formats a card: NoCard / NotFormatted go back to the caller (ask the player first).
 * - Everything is blocking (psyqo pumps GPU callbacks meanwhile, so timers such as the loading
 *   icon keep running). psyqo's SIO0 lock pauses AdvancedPad polling during card transfers.
 * - An existing file is replaced by delete + write (not atomic: a power cut in between loses it).
 *
 * Card images and checks on the host: tools/memcard/mcd.py. Icons: make mcicons.
 */
#pragma once

#include <stdint.h>

#include <psyqo/memory-card-filesystem.hh>

using PS1McError = psyqo::MemoryCard::Error;
using PS1McPort  = psyqo::MemoryCard::Port;
using PS1McIcon  = psyqo::MemoryCardFileSystem::Icon;

#define PS1_MC_HEADER_SIZE 16

void PS1McPrepare(); // from Application::prepare(), before any other call
const char *PS1McErrorMessage(PS1McError error);

// "B" + region ('A' America, 'E' Europe, 'I' Japan) + product ("SLUS-01234", 10 chars) + name
// (<= 8 chars). Returns false if the parts don't fit the 20-character Sony name.
bool PS1McFileName(char out[21], char region, const char *product, const char *name);

PS1McError PS1McState(PS1McPort port); // OK, NoCard, NotFormatted, ...
PS1McError PS1McFreeBlocks(PS1McPort port, uint32_t *outFree);
PS1McError PS1McExists(PS1McPort port, const char *file, bool *outExists);
PS1McError PS1McList(PS1McPort port, psyqo::MemoryCardFileSystem::FileEntry *out, uint32_t max, uint32_t *outCount);
PS1McError PS1McInfo(PS1McPort port, const char *file, psyqo::MemoryCardFileSystem::FileInfo *out);
PS1McError PS1McSave(PS1McPort port, const char *file, const char *title, const PS1McIcon &icon, const void *data,
                     uint32_t len, uint16_t version);
// SerializeOverflow if the saved data is longer than maxLen, BadData if the header or CRC is wrong.
PS1McError PS1McLoad(PS1McPort port, const char *file, void *buf, uint32_t maxLen, uint32_t *outLen,
                     uint16_t *outVersion);
PS1McError PS1McDelete(PS1McPort port, const char *file);

// Mania (docs/32 phase 8): no heap copies (the heap is STG's at 2 MB). `buf` holds PS1_MC_HEADER_SIZE bytes of room,
// then the data: the header is written in place, the file replaced by psyqo's writeFile (data first, directory last;
// on OutOfSpace an existing file is deleted and the write retried). Load: the data lands at buf + PS1_MC_HEADER_SIZE.
PS1McError PS1McSaveInPlace(PS1McPort port, const char *file, const char *title, const PS1McIcon &icon, uint8_t *buf,
                            uint32_t len, uint16_t version);
PS1McError PS1McLoadInPlace(PS1McPort port, const char *file, uint8_t *buf, uint32_t cap, uint32_t *outLen,
                            uint16_t *outVersion);
// In the background (TMC's PS1McSaveAsync, without its heap copy): `buf` (header room + data) must stay untouched until
// `done` runs; one at a time. The blocking calls above wait for it first (PS1McWaitIdle).
PS1McError PS1McSaveInPlaceAsync(PS1McPort port, const char *file, const char *title, const PS1McIcon &icon, uint8_t *buf,
                                 uint32_t len, uint16_t version, void (*done)(PS1McError));
bool PS1McBusy();
void PS1McWaitIdle();
