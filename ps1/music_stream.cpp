/*
 * Streamed SPU music (docs/ports/mania/63 M5). See music_stream.hh for the design.
 *
 * Life of one stream:
 *   Resolve (boot)   PS1FsFind the body: LBA, bytes -> sectors, and the loop sector from the loop point.
 *   Begin            no disc traffic yet; records the ring address and asks for the first read.
 *   WAIT_DRIVE       the XA action is still winding down (one CD action at a time): wait for it to end.
 *   WAIT_DATA        PS1StreamOpen at the body, pump until one sector is in the ring (a sector is exactly the 2 KiB
 *                    ring: both halves filled), then set the repeat address and key the voice on.
 *   PLAYING          each pump: the voice's current address says which half it is in, so the other half is free and
 *                    receives the next 1,024 bytes; the drive is restarted by the sector ring's own low-water pump,
 *                    and when the run's sectors are all consumed the read is re-opened at the loop sector (the seam).
 *   End              voice off now; the disc read is closed without waiting, and Closing() stays true until the
 *                    action ended, which is what lets the XA music start.
 */
#include "music_stream.hh"

#include <string.h>
#include <psyqo/alloc.h>
#include <psyqo/gpu.hh>
#include "cdrom_stream.hh"
#include "spu.hh"

bool PS1FsFind(const char *path, uint32_t *lba, uint32_t *size); // ps1/cdrom_fs.cpp
void PS1XaStopAsync();                                           // ps1/xa_music.cpp
bool PS1XaIdle();                                                // ps1/xa_music.cpp
uint32_t PS1Hblanks();                                          // ps1/video.cpp: root counter 1
extern psyqo::GPU *g_ps1GPU;                                    // ps1/rsdk5_main.cpp

volatile uint32_t g_ps1MusicStreamStarts    = 0;
volatile uint32_t g_ps1MusicStreamHalves    = 0;
volatile uint32_t g_ps1MusicStreamUnderruns = 0;
volatile uint32_t g_ps1MusicStreamLoops     = 0;
volatile uint32_t g_ps1MusicStreamBytes     = 0;
volatile uint32_t g_ps1MusicStreamLastPos   = 0;
// The sector the body read is about to play (main RAM) and its index in the body: a GDB dump of the first compares
// with the built .adp byte for byte, which is the disc -> RAM half of a playback claim (the CPU then DMAs that same
// buffer into the ring).
const uint8_t *g_ps1MusicStreamSector;
volatile uint32_t g_ps1MusicStreamRing = 0;
// why a Begin was declined (GDB): 1 no body resolved, 2 a stream already runs, 3 the ring is out of the sample area
// (a bank leaves less than the ring needs), 4 a zero rate, 0 none
volatile uint32_t g_ps1MusicStreamFail = 0;
volatile uint32_t g_ps1MusicStreamPhase = 0;
volatile uint32_t g_ps1MusicStreamBlocker = 0; // WAIT_DRIVE's blocker: 1 own action, 2 XA action, 3 heap, 4 open
volatile uint32_t g_ps1MusicStreamRingSectors = 0;
volatile uint32_t g_ps1MusicStreamReserved = 0;
volatile uint32_t g_ps1MusicStreamForcedStops = 0; // a file read took the drive (the music stops, like the XA) // the ring was reserved at boot (kept for good) // the pump's phase (0 idle, 1 wait drive, 2 wait data, 3 playing, 4 closing)
volatile uint32_t g_ps1MusicStreamSrcSector = 0;

namespace {

constexpr uint32_t SECTOR = 2048;

enum Phase { PH_IDLE, PH_WAIT_DRIVE, PH_WAIT_DATA, PH_PLAYING, PH_CLOSING };

uint32_t s_lba;         // the body's first sector
uint32_t s_bodySectors; // the body's length in sectors
uint32_t s_loopSector;  // where the loop restarts (the loop point's sector)
uint32_t s_ringAddr;    // the SPU ring's address
uint16_t s_rate;        // the body's rate (22,050)
uint16_t s_volL, s_volR;
uint8_t *s_cdRing;                     // s_cdSectors * 2048 in main RAM (less when the heap is tight)
uint32_t s_cdSectors = PS1_MUSIC_CD_SECTORS;
uint8_t s_half[PS1_MUSIC_HALF_BYTES];  // the staging buffer a half is patched in before the SPU DMA

Phase s_phase;
uint32_t s_runFrom;   // the body sector the current read run starts at (0, then the loop sector)
uint32_t s_srcSector; // the body sector the sector ring's oldest entry holds
uint32_t s_off;       // bytes of the current sector already handed to the ring (0 or HALF)
uint32_t s_fillHalf;  // halves filled at start-up (0..2)
uint32_t s_dueHbl;    // when the next half must be written (hblanks; the ring's clock, not the emulator's register)
uint32_t s_hblPerHalf;
int32_t s_playHalf;   // the half the voice is in according to its address register (-1 before key-on)
int32_t s_fillNext;   // the next half to refill when the register does not advance (alternates 0, 1, 0, ...)
bool s_paused;

uint32_t align8(uint32_t v) { return (v + 7u) & ~7u; }

void writeHalf(uint32_t half, const uint8_t *src) {
    memcpy(s_half, src, PS1_MUSIC_HALF_BYTES);
    if (half == 1) // the ring's last block: loop end + loop repeat -> the voice wraps to the repeat address
        s_half[PS1_MUSIC_HALF_BYTES - 15] |= 0x03;
    PS1SpuWrite(s_ringAddr + half * PS1_MUSIC_HALF_BYTES, s_half, PS1_MUSIC_HALF_BYTES);
    g_ps1MusicStreamBytes = g_ps1MusicStreamBytes + PS1_MUSIC_HALF_BYTES;
}

// The next 1,024 bytes of the body from the sector ring; false when no sector is ready.
bool nextHalf(const uint8_t *&src) {
    if (PS1StreamAvailable() == 0)
        return false;
    src = PS1StreamSector(0) + s_off;
    s_off += PS1_MUSIC_HALF_BYTES;
    if (s_off >= SECTOR) { // the sector is fully used: release it so the drive may refill
        s_off = 0;
        PS1StreamConsume(1);
        s_srcSector = s_srcSector + 1;
    }
    return true;
}

// The sector ring, at the widest size the heap allows (12 sectors = 975 ms; 2 sectors = 162 ms is the floor).
bool allocRing() {
    if (s_cdRing) {
        g_ps1MusicStreamRingSectors = s_cdSectors;
        return true;
    }
    for (s_cdSectors = PS1_MUSIC_CD_SECTORS; s_cdSectors >= 2; s_cdSectors /= 2) {
        s_cdRing = (uint8_t *)psyqo_malloc(s_cdSectors * SECTOR);
        if (s_cdRing) {
            g_ps1MusicStreamRingSectors = s_cdSectors;
            return true;
        }
    }
    return false;
}

bool openRead(uint32_t from) {
    s_srcSector = from;
    if (!allocRing())
        return false;
    // false when another CD action still runs (the atlas group this music brought loads first): retry next frame
    return PS1StreamOpen(s_lba + from, s_bodySectors - from, s_cdRing, s_cdSectors);
}

} // namespace

// Reserve the sector ring now: the engine's pool claims the heap during a scene, so a malloc at the transform finds
// nothing (measured 2026-11-09: even 2 KB failed). Called from the audio device's boot, where the heap is still free.
void PS1MusicStreamSetRing(void *mem, uint32_t sectors) {
    s_cdRing                   = (uint8_t *)mem;
    s_cdSectors                = sectors;
    g_ps1MusicStreamReserved   = 1;
    g_ps1MusicStreamRingSectors = sectors;
}

bool PS1MusicStreamReserve() {
    if (!allocRing())
        return false;
    g_ps1MusicStreamReserved = 1;
    return true;
}

bool PS1MusicStreamResolve(const char *discPath) {
    uint32_t lba = 0, size = 0;
    if (!discPath || !PS1FsFind(discPath, &lba, &size) || size < SECTOR)
        return false;
    s_lba         = lba;
    s_bodySectors = (size + SECTOR - 1) / SECTOR;
    return true;
}

bool PS1MusicStreamBegin(uint32_t loopSample44, uint32_t rate, uint32_t spuAddr) {
    if (!s_lba) {
        g_ps1MusicStreamFail = 1;
        return false;
    }
    if (s_phase != PH_IDLE) {
        g_ps1MusicStreamFail = 2;
        return false;
    }
    if (!rate) {
        g_ps1MusicStreamFail = 4;
        return false;
    }
    if ((spuAddr & 7u) || spuAddr < PS1_SPU_SAMPLE_START || spuAddr + PS1_MUSIC_RING_BYTES > PS1_SPU_SAMPLE_END) {
        g_ps1MusicStreamFail = 3;
        return false;
    }
    // The loop restarts at the sector holding the loop point: 44.1 kHz sample -> the body's samples -> ADPCM block
    // (28 samples) -> byte offset (16 bytes a block). Rounding down replays at most one block group (23 ms) at the
    // seam, which is what keeps the seam sector-aligned - a CD read can only start on a sector.
    uint64_t samples = (uint64_t)loopSample44 * rate / 44100u;
    uint32_t byteOff = (uint32_t)((samples / 28u) * 16u);
    s_loopSector     = byteOff / SECTOR;
    if (s_loopSector >= s_bodySectors)
        s_loopSector = 0;
    s_ringAddr            = spuAddr;
    g_ps1MusicStreamRing  = spuAddr; // GDB: no function calls on this stub, so the ring's address is a global
    s_rate      = (uint16_t)rate;
    s_off      = 0;
    s_fillHalf = 0;
    s_playHalf = -1;
    s_fillNext = 0;
    s_paused   = false;
    s_runFrom    = 0;
    // the ring's clock: one half (1,024 B = 1,792 samples) at the body's rate, in hblanks
    s_hblPerHalf = (uint32_t)((uint64_t)1792u * 15734u / rate);
    s_dueHbl     = PS1Hblanks() + s_hblPerHalf;
    s_phase      = PH_WAIT_DRIVE;
    PS1XaStopAsync(); // one CD action at a time; the XA action ends in a few vblanks
    return true;
}

void PS1MusicStreamPump() {
    g_ps1MusicStreamPhase = (uint32_t)s_phase;
    if (s_phase == PH_CLOSING) {
        if (!PS1StreamRunning())
            s_phase = PH_IDLE;
        return;
    }
    if (s_phase == PH_IDLE)
        return;
    if (s_phase == PH_WAIT_DRIVE) {
        if (PS1StreamRunning()) {
            g_ps1MusicStreamBlocker = 1;
            return;
        }
        if (!PS1XaIdle()) {
            g_ps1MusicStreamBlocker = 2;
            return;
        }
        if (!allocRing()) {
            g_ps1MusicStreamBlocker = 3;
            return;
        }
        g_ps1MusicStreamRingSectors = s_cdSectors;
        if (!openRead(s_runFrom)) {
            g_ps1MusicStreamBlocker = 4;
            return; // the drive is busy with another action (a file read or an atlas load): retried next frame
        }
        s_phase = PH_WAIT_DATA;
    }
    if (!s_paused)
        PS1StreamPump();
    const uint8_t *src = nullptr;
    if (s_phase == PH_WAIT_DATA) {
        while (s_fillHalf < 2) {
            if (!nextHalf(src))
                return; // the sector has not arrived yet: the next pump continues
            writeHalf(s_fillHalf++, src);
        }
        // the voice: repeat address = the ring's start so playback wraps inside the ring, at the body's rate
        PS1SpuVoiceRepeat(PS1_MUSIC_STREAM_VOICE, s_ringAddr);
        PS1SpuVoicePlay(PS1_MUSIC_STREAM_VOICE, s_ringAddr, s_rate, s_volL, s_volR);
        s_playHalf             = 0;
        s_fillNext             = 0; // half 0 plays first, so half 0 is the first one to refill
        s_dueHbl               = PS1Hblanks() + s_hblPerHalf;
        s_phase                = PH_PLAYING;
        g_ps1MusicStreamStarts = g_ps1MusicStreamStarts + 1;
        return;
    }
    // PLAYING: which half is free. The voice's current address is the authority when the emulator updates it; the
    // ring's own clock (1,279 hblanks a half at 22,050 Hz) covers the case where it does not (PCSX-Redux left the
    // address at the ring's start, measured 2026-11-09), so a half is written early rather than missed.
    uint32_t pos            = PS1SpuVoicePosition(PS1_MUSIC_STREAM_VOICE);
    g_ps1MusicStreamLastPos = pos;
    g_ps1MusicStreamSector    = PS1StreamAvailable() ? PS1StreamSector(0) : g_ps1MusicStreamSector;
    g_ps1MusicStreamSrcSector = s_srcSector;
    if (s_paused)
        return;
    bool inRing  = pos >= s_ringAddr && pos < s_ringAddr + PS1_MUSIC_RING_BYTES;
    int32_t half = inRing ? (int32_t)((pos - s_ringAddr) / PS1_MUSIC_HALF_BYTES) : s_playHalf;
    bool moved   = inRing && half != s_playHalf;
    uint32_t now = PS1Hblanks();
    bool due     = (int16_t)((uint32_t)(now - s_dueHbl)) >= 0;
    if (!moved && !due)
        return;
    // the free half is the one the voice has just left; without a moving register, alternate
    int32_t fill = moved ? (int32_t)(1 - half) : s_fillNext;
    if (moved)
        s_playHalf = half;
    if (!nextHalf(src)) { // the sector ring is empty: the half still holds its audio, nothing is written this frame
        g_ps1MusicStreamUnderruns = g_ps1MusicStreamUnderruns + 1;
        s_dueHbl                  = now + 4; // retry soon
        return;
    }
    s_fillNext = (int32_t)(1 - fill);
    s_dueHbl   = now + s_hblPerHalf;
    writeHalf((uint32_t)fill, src);
    g_ps1MusicStreamHalves = g_ps1MusicStreamHalves + 1;
    if (PS1StreamEnded()) { // the body ran out: re-open at the loop sector; the ring's remaining sectors cover the pause
        PS1StreamCloseAsync();
        s_runFrom             = s_loopSector;
        s_off                 = 0;
        s_phase               = PH_WAIT_DRIVE;
        g_ps1MusicStreamLoops = g_ps1MusicStreamLoops + 1;
    }
    // for GDB: the buffer the next bytes come from, and where it sits in the body
    g_ps1MusicStreamSector    = PS1StreamAvailable() ? PS1StreamSector(0) : g_ps1MusicStreamSector;
    g_ps1MusicStreamSrcSector = s_srcSector;
}

void PS1MusicStreamVolume(uint16_t volL, uint16_t volR) {
    if (volL == s_volL && volR == s_volR)
        return;
    s_volL = volL, s_volR = volR;
    if (s_phase == PH_PLAYING)
        PS1SpuVoiceVolume(PS1_MUSIC_STREAM_VOICE, volL, volR);
}

void PS1MusicStreamPause(bool paused) {
    if (paused == s_paused)
        return;
    s_paused = paused;
    if (s_phase != PH_PLAYING)
        return;
    PS1SpuVoicePitch(PS1_MUSIC_STREAM_VOICE, paused ? 0 : (uint16_t)((uint32_t)s_rate * PS1_SPU_PITCH_UNIT / 44100u));
}

bool PS1MusicStreamActive() { return s_phase == PH_WAIT_DRIVE || s_phase == PH_WAIT_DATA || s_phase == PH_PLAYING; }
bool PS1MusicStreamPlaying() { return s_phase == PH_PLAYING; }
bool PS1MusicStreamClosing() { return s_phase == PH_CLOSING; }
uint32_t PS1MusicStreamRingAddr() { return s_ringAddr; }

// A file read is the other owner of the drive (ps1/cdrom_fs.cpp's safety net, as for the XA): stop the voice, close
// the read and wait for the action to end (bounded), so psyqo never sees two actions registered at once.
void PS1MusicStreamStopForFileRead() {
    if (s_phase == PH_IDLE)
        return;
    g_ps1MusicStreamForcedStops = g_ps1MusicStreamForcedStops + 1;
    PS1MusicStreamEnd();
    uint32_t last = PS1Hblanks(), waited = 0;
    while (PS1StreamRunning() && g_ps1GPU) {
        g_ps1GPU->pumpCallbacks();
        uint32_t now = PS1Hblanks();
        waited += (now - last) & 0xffff;
        last = now;
        if (waited > 15734 / 5) // 200 ms: a healthy PAUSE answers in ~7 vblanks
            break;
    }
}

void PS1MusicStreamEnd() {
    if (s_phase == PH_IDLE)
        return;
    PS1SpuVoiceStop(1u << PS1_MUSIC_STREAM_VOICE);
    if (s_phase != PH_CLOSING) {
        PS1StreamCloseAsync();
        s_phase = PH_CLOSING;
    }
    if (s_cdRing && !g_ps1MusicStreamReserved) { // a late allocation goes back to the heap (M1 atlas work); a boot one stays
        psyqo_free(s_cdRing);
        s_cdRing = nullptr;
    }
    s_playHalf = -1;
    s_off      = 0;
    s_fillHalf = 0;
}
