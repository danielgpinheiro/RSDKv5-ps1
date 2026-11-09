/*
 * Streamed SPU music (docs/ports/mania/63 M5): a music body stays on the disc and plays from a 2 KiB SPU ring on a
 * voice the engine never uses, so a theme that cannot be resident (the Super theme is 500,976 B) plays with no load
 * stall at the transform and no handheld decoding - only DMA of the bytes the disc delivers.
 *
 * Body: raw SPU-ADPCM, 22,050 Hz mono, encoded at build time (tools/audio/build_stream.py: psxavenc -t vag -f 22050),
 * one `.adp` file on the disc (Data/Music/Super.adp). Played at pitch 0x800, which is what makes 44.1 kHz frames
 * decode at 22,050 Hz: an ADPCM frame carries its samples at the rate it was encoded at.
 *
 * Ring: 2,048 B in the SFX sample area, above the scene's bank, two 1,024 B halves. The block at the ring's end
 * carries the loop-end + loop-repeat flags and the voice's repeat address is the ring's start, so playback wraps
 * inside the ring while the half the voice is not in is refilled. Nothing is ever decoded on the CPU.
 *
 * Bytes come from the port's CD sector ring (ps1/cdrom_stream.hh): a 4-sector main-RAM ring (8 KB, 325 ms of audio)
 * restarted at half-full, which covers the PAUSE (~7 vblanks) and the seek at the body's loop seam.
 *
 * One CD action at a time: start this only after the XA music stopped, and do not read files while it runs.
 */
#pragma once

#include <stdint.h>

#define PS1_MUSIC_STREAM_VOICE 17 // voices 1-16 are the engine's (channel c -> voice c + 1)
#define PS1_MUSIC_RING_BYTES   2048u
#define PS1_MUSIC_HALF_BYTES   1024u
#define PS1_MUSIC_CD_SECTORS   2u // 4 KB: 162 ms of audio; the restart at half holds 81 ms, the seam's floor
#define PS1_MUSIC_CD_BYTES     (PS1_MUSIC_CD_SECTORS * 2048u)

// Storage.cpp's boot layout hands the ring here (4 KB taken before the init reserve): no malloc at any later point, so
// the GPU chain arena's block is never split. Call it before PS1MusicStreamBegin can run.
void PS1MusicStreamSetRing(void *mem, uint32_t sectors);

// Boot: find the body on the disc once (a directory read, never at the transform). False when it is absent.
bool PS1MusicStreamResolve(const char *discPath);
// Boot, right after Resolve: reserve the sector ring while the heap is still free (the engine's pool claims it during
// a scene and no malloc succeeds then: measured 2026-11-09, even 2 KB failed). False when the heap is already gone.
bool PS1MusicStreamReserve();

// The transform: claim the SPU ring at `spuAddr` (the caller places it above the loaded SFX bank) and start reading.
// Non-blocking: the pump opens the disc read and keys the voice on once both halves are filled. Returns false (and
// starts nothing) when the body was not resolved, is already streaming, or the ring would not fit below
// PS1_SPU_SAMPLE_END - the caller then keeps the XA path.
bool     PS1MusicStreamBegin(uint32_t loopSample44, uint32_t rate, uint32_t spuAddr);
void     PS1MusicStreamPump();             // once a frame: refills the free half, restarts the drive when it runs low
void     PS1MusicStreamVolume(uint16_t volL, uint16_t volR);
void     PS1MusicStreamPause(bool paused); // pitch 0 holds the voice (spu.hh); the drive read stops too
bool     PS1MusicStreamActive();           // begun and not ended (the voice may still be filling its first ring)
bool     PS1MusicStreamPlaying();          // the voice is on
bool     PS1MusicStreamClosing();          // end requested, the CD action has not ended yet
void     PS1MusicStreamEnd();              // stop the voice now, close the disc read without waiting
// A file read takes the drive (cdrom_fs's safety net, like the XA's): stop and wait for the action to end, bounded.
void     PS1MusicStreamStopForFileRead();
uint32_t PS1MusicStreamRingAddr();         // the SPU address of the ring

// Verification counters (GDB): what a playback claim is checked against, never on its own.
extern volatile uint32_t g_ps1MusicStreamStarts;      // bodies started (voice keyed on)
extern volatile uint32_t g_ps1MusicStreamHalves;      // halves refilled
extern volatile uint32_t g_ps1MusicStreamUnderruns;   // refills that found no sector (an audible gap)
extern volatile uint32_t g_ps1MusicStreamLoops;       // passes through the loop seam
extern volatile uint32_t g_ps1MusicStreamBytes;       // body bytes DMA'd into the ring
extern volatile uint32_t g_ps1MusicStreamLastPos;     // the voice's address at the last refill
extern const uint8_t *g_ps1MusicStreamSector;         // the sector ring's next buffer (main RAM, dumpable)
extern volatile uint32_t g_ps1MusicStreamSrcSector;   // that buffer's sector index in the body
extern volatile uint32_t g_ps1MusicStreamRing;        // the ring's SPU address (a global: no calls from GDB)
extern volatile uint32_t g_ps1MusicStreamFail;        // why the last Begin was declined (1 no body, 2 busy, 3 no room, 4 rate)
extern volatile uint32_t g_ps1MusicStreamPhase;       // 0 idle, 1 wait drive, 2 wait data, 3 playing, 4 closing
extern volatile uint32_t g_ps1MusicStreamBlocker;     // WAIT_DRIVE's blocker (1 own action, 2 XA, 3 heap, 4 open)
extern volatile uint32_t g_ps1MusicStreamRingSectors; // the sector ring's size when it was allocated
extern volatile uint32_t g_ps1MusicStreamReserved;    // the ring was reserved at boot (never freed)
extern volatile uint32_t g_ps1MusicStreamForcedStops; // a file read took the drive (the music stopped)
