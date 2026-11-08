/*
 * CD-XA music — see xa_music.hh (from the Sonic 2 port; Mania: docs/ports/mania/32 5.2). Action pattern from ps1/cdrom_stream.cpp; XA command sequence
 * from PSn00bSDK examples/cdrom/cdxa and psx-spx cdromdrive.md (Setfilter, Setmode, ReadS).
 */
#include "xa_music.hh"

#include <EASTL/atomic.h>
#include <stdio.h>
#include <string.h>

#include "common/hardware/dma.h"
#include "psyqo/cdrom-device.hh"
#include "psyqo/gpu.hh"
#include "psyqo/hardware/cdrom.hh"
#include "psyqo/hardware/sbus.hh"
#include "psyqo/kernel.hh"
#include "psyqo/msf.hh"

extern psyqo::CDRomDevice *g_ps1CDRom;
extern psyqo::GPU *g_ps1GPU;
extern volatile uint32_t g_ps1PerfFrames;
bool PS1FsFind(const char *path, uint32_t *lba, uint32_t *size); // cdrom_fs.cpp
void PS1FsStopStream();                                          // cdrom_fs.cpp: one CD action at a time
uint32_t PS1Hblanks();                                           // video.cpp: root counter 1
uint32_t PS1HblanksSince(uint32_t t0);                            // video.cpp: 16-bit wrap safe

volatile uint32_t g_ps1XaStarts      = 0;
volatile uint32_t g_ps1XaLoops       = 0;
volatile uint32_t g_ps1XaEnds        = 0;
volatile uint32_t g_ps1XaDataSectors = 0;
volatile uint32_t g_ps1XaForcedStops = 0;
volatile uint32_t g_ps1XaAborts       = 0; // waits for the drive's PAUSE that timed out: the action was ended by abort()
volatile uint32_t g_ps1XaGiveUps      = 0; // ... and the drive didn't answer the abort either (last resort)
volatile uint32_t g_ps1XaLoopFrame   = 0;
volatile uint32_t g_ps1XaLoopGapHbl    = 0;
volatile uint32_t g_ps1XaLoopGapMaxHbl = 0;
volatile uint32_t g_ps1XaSeamGainLast = 0x7FFF;
volatile uint32_t g_ps1XaSeamMode     = 0; // 0 none, 1 fade out, 2 hold, 3 fade in
volatile int32_t g_ps1XaSeamOff       = 0;
volatile uint32_t g_ps1XaStartFrame  = 0;
volatile int32_t g_ps1XaChannel      = -1;
volatile int32_t g_ps1XaTrack        = -1;

using psyqo::Hardware::CDRom::CDL;

namespace {

#define XA_FILE       1
#define XA_MODE       0xE8 // 2x speed | XA-ADPCM | 2340-byte sectors | XA filter
#define XA_SECTOR     2340
#define XA_MAX_TRACK  64 // Mania: 62 tracks in scope
#define XA_INTERLEAVE 8
#define XA_MARKER     24 // bytes of a data sector read: header, subheader, 'RSDKXAEND', channel, u16 id (the rest is left
                         // in the drive's buffer, as PSn00bSDK's CdGetSector reads part of a sector)

struct Track { // Music.bin XAM5 entry (tools/audio/build_music.py), 12 bytes
    uint32_t hash; // FNV-1a of the lower-case name
    uint16_t startRow, rows, pad;
    uint8_t channel, id;
};
static_assert(sizeof(Track) == 12, "Music.bin entry");
Track s_tracks[XA_MAX_TRACK];
int s_trackCount    = 0;
uint32_t s_fileLba  = 0;
bool s_ready        = false;

enum class XaState : uint8_t { IDLE, SETLOC, SETFILTER, SETMODE, READS, PLAYING, LOOP_SETLOC, LOOP_READS, PAUSE, PAUSE_ACK };

struct Xa {
    volatile bool running = false; // an action is registered
    volatile bool ended   = false; // non-looping track reached its end marker
    volatile bool paused  = false; // PauseSound: resumable
    int channel           = -1;
    int track             = -1;
    bool loop             = false;
    uint32_t offset       = 0; // start row within the track (PlayStream's startPos)
    uint32_t loopRow      = 0; // the loop's row within the track (PlayStream's loopPoint)
    // Playing position (rows from the track's start): posBase at time posT0 (psyqo::GPU::now(), us), 18.75 rows/s;
    // rebased at the start, each loop (IRQ) and pause.
    volatile uint32_t posBase = 0;
    volatile uint32_t posT0   = 0;
    uint32_t gapT0            = 0; // hblanks at the last end marker (where a loop's gap starts)
    // docs/ports/mania/63 M8: the CD->SPU path reads ahead of what is heard, so a position-derived fade fires early.
    // The end marker tells the CPU where the drive is, so the seam is timed from it (and from the loop's ReadS ack).
    volatile uint32_t seamMode = 0;
    uint32_t seamT0            = 0;
    // docs/ports/mania/32 9f: a stop that doesn't wait (PS1XaStopAsync: the drive's Pause takes ~1,800 hblanks on a real drive /
    // DuckStation, a 7-vblank freeze each time the music stopped or changed) and a start queued until the drive is
    // idle (PS1XaService, each frame)
    volatile bool stopping = false;
    struct {
        bool on;
        int track;
        bool loop;
        uint32_t row, loopRow;
    } pend = {};
};
Xa s_xa;

uint32_t nowUs() { return g_ps1GPU ? g_ps1GPU->now() : 0; }

alignas(4) uint8_t s_sector[XA_MARKER];

void sendSetloc(uint32_t lba) {
    psyqo::MSF msf(lba + 150);
    uint8_t bcd[3];
    msf.toBCD(bcd);
    psyqo::Hardware::CDRom::Command.send(CDL::SETLOC, bcd[0], bcd[1], bcd[2]);
}

// Physical sector of row r of the current channel (row r of channel k = file sector r * 8 + k).
uint32_t rowLba(uint32_t row) { return s_fileLba + row * XA_INTERLEAVE + (uint32_t)s_xa.channel; }

class XaAction : public psyqo::CDRomDevice::Action<XaState> {
  public:
    XaAction() : Action("PS1XaAction") {}
    bool reading() const { return getState() == XaState::PLAYING; }
    void start(bool resume) {
        registerMe(g_ps1CDRom);
        setCallback([](bool) {
            s_xa.running = false;
            eastl::atomic_signal_fence(eastl::memory_order_release);
        });
        if (resume) { // the drive keeps mode, filter and position across a Pause
            setState(XaState::READS);
            psyqo::Hardware::CDRom::Command.send(CDL::READS);
        } else {
            setState(XaState::SETLOC);
            sendSetloc(rowLba(s_tracks[s_xa.track].startRow + s_xa.offset));
        }
    }
    // From the main loop: pause if playing (the IRQ may be mid-sequence: then on its next ack).
    void requestPause() {
        psyqo::Kernel::fastEnterCriticalSection();
        m_pauseRequested = true;
        if (getState() == XaState::PLAYING) {
            setState(XaState::PAUSE);
            psyqo::Hardware::CDRom::Command.send(CDL::PAUSE);
        }
        psyqo::Kernel::fastLeaveCriticalSection();
    }
    void clearPauseRequest() { m_pauseRequested = false; }
    // From the main loop, when the drive hasn't answered for XA_WAIT_HBL (waitIdle): make it answer and end the
    // action through psyqo's own IRQ path (a plain timeout would leave the action registered: psyqo then asserts on
    // the next one). PAUSE always answers INT3 and stops the drive; the next response of any kind ends the action
    // with success false (the callback runs, s_xa.running goes false). A later response of the abandoned command
    // finds no action: ps1/irq_guard.cpp drains it. (From the RSDKv4 port, 75ddf41.)
    void abort() {
        psyqo::Kernel::fastEnterCriticalSection();
        m_abort          = true;
        m_pauseRequested = true;
        psyqo::Hardware::CDRom::Command.send(CDL::PAUSE);
        psyqo::Kernel::fastLeaveCriticalSection();
    }
    void clearAbort() { m_abort = false; }
    bool aborted() {
        if (!m_abort)
            return false;
        m_abort = false;
        setSuccess(false);
        return true;
    }
    bool dataReady(const psyqo::CDRomDevice::Response &) override {
        // Only data sectors reach the CPU in XA mode: read it (header + subheader + data) and
        // look for our channel's end marker.
        psyqo::Hardware::CDRom::Ctrl.throwAway();
        psyqo::Hardware::CDRom::DataRequest = 0;
        psyqo::Hardware::CDRom::InterruptControl.throwAway();
        psyqo::Hardware::CDRom::DataRequest = 0x80;
        psyqo::Hardware::SBus::Dev5Ctrl = 0x20943;
        psyqo::Hardware::SBus::ComCtrl  = 0x132c;
        eastl::atomic_signal_fence(eastl::memory_order_acquire);
        DMA_CTRL[DMA_CDROM].MADR = reinterpret_cast<uintptr_t>(s_sector);
        DMA_CTRL[DMA_CDROM].BCR  = (XA_MARKER / 4) | 0x10000;
        DMA_CTRL[DMA_CDROM].CHCR = 0x11000000; // burst: completes before the CPU continues
        g_ps1XaDataSectors = g_ps1XaDataSectors + 1;
        // 2340-byte layout: 4-byte header (MSF, mode), 8-byte subheader (file, channel, submode,
        // coding, repeated), then the 2048 data bytes: 'RSDKXAEND', channel, u16 track id.
        const Track &t = s_tracks[s_xa.track];
        bool marker    = s_sector[4] == XA_FILE && s_sector[5] == s_xa.channel && (s_sector[6] & 0x08)
                      && !memcmp(s_sector + 12, "RSDKXAEND", 9) && s_sector[21] == s_xa.channel
                      && (s_sector[22] | (s_sector[23] << 8)) == t.id;
        if (marker && getState() == XaState::PLAYING) {
            if (s_xa.loop && !m_pauseRequested) {
                g_ps1XaLoops     = g_ps1XaLoops + 1;
                g_ps1XaLoopFrame = g_ps1PerfFrames;
                s_xa.posBase     = s_xa.loopRow;
                s_xa.posT0       = nowUs();
                s_xa.gapT0       = PS1Hblanks();
                s_xa.seamMode    = 1; // fade out while the buffered tail plays
                s_xa.seamT0      = s_xa.gapT0;
                setState(XaState::LOOP_SETLOC);
                sendSetloc(rowLba(t.startRow + s_xa.loopRow));
            } else {
                g_ps1XaEnds = g_ps1XaEnds + 1;
                s_xa.ended  = true;
                setState(XaState::PAUSE);
                psyqo::Hardware::CDRom::Command.send(CDL::PAUSE);
            }
        }
        eastl::atomic_signal_fence(eastl::memory_order_release);
        return aborted(); // the sector was read: an abort ends the action here
    }
    bool complete(const psyqo::CDRomDevice::Response &) override {
        if (aborted())
            return true;
        if (getState() != XaState::PAUSE_ACK)
            return false;
        setSuccess(true);
        return true;
    }
    bool acknowledge(const psyqo::CDRomDevice::Response &) override {
        if (aborted())
            return true;
        switch (getState()) {
            case XaState::SETLOC:
                setState(XaState::SETFILTER);
                psyqo::Hardware::CDRom::Command.send(CDL::SETFILTER, XA_FILE, (uint8_t)s_xa.channel);
                break;
            case XaState::SETFILTER:
                setState(XaState::SETMODE);
                psyqo::Hardware::CDRom::Command.send(CDL::SETMODE, XA_MODE);
                break;
            case XaState::SETMODE:
                setState(XaState::READS);
                psyqo::Hardware::CDRom::Command.send(CDL::READS);
                break;
            case XaState::LOOP_SETLOC:
                setState(XaState::LOOP_READS);
                psyqo::Hardware::CDRom::Command.send(CDL::READS);
                break;
            case XaState::READS:
            case XaState::LOOP_READS: {
                // the drive has the loop's read: this, not the marker, is where the audio comes back
                uint32_t gap      = PS1HblanksSince(s_xa.gapT0);
                g_ps1XaLoopGapHbl = gap;
                if (gap > g_ps1XaLoopGapMaxHbl)
                    g_ps1XaLoopGapMaxHbl = gap;
                if (getState() == XaState::LOOP_READS) {
                    s_xa.posT0    = nowUs(); // the position follows the drive's answer, not the marker
                    s_xa.seamMode = 2;       // hold, then fade in once the drive's refill is playing
                    s_xa.seamT0   = s_xa.gapT0;
                }
                if (m_pauseRequested) {
                    setState(XaState::PAUSE);
                    psyqo::Hardware::CDRom::Command.send(CDL::PAUSE);
                } else {
                    setState(XaState::PLAYING);
                }
                break;
            }
            case XaState::PAUSE:
                setState(XaState::PAUSE_ACK);
                break;
            default:
                break;
        }
        return false;
    }

  private:
    volatile bool m_pauseRequested = false;
    volatile bool m_abort          = false;
};

XaAction s_action;

// Demute (psx-spx: "Demute - Command 0Ch --> INT3(stat)"). psyqo's CDRomDevice::unmute() waits for
// an INT2 (complete) that never comes and aborts on the INT3 ("acknowledge() not implemented"),
// so the port sends it itself.
enum class DemuteState : uint8_t { IDLE, DEMUTE };
class DemuteAction : public psyqo::CDRomDevice::Action<DemuteState> {
  public:
    DemuteAction() : Action("PS1DemuteAction") {}
    void start(volatile bool *done) {
        registerMe(g_ps1CDRom);
        m_done = done;
        setCallback([this](bool) { *m_done = true; });
        setState(DemuteState::DEMUTE);
        psyqo::Hardware::CDRom::Command.send(CDL::UNMUTE);
    }
    bool acknowledge(const psyqo::CDRomDevice::Response &) override {
        setSuccess(true);
        return true;
    }

  private:
    volatile bool *m_done = nullptr;
};
DemuteAction s_demute;

// PS1XaStop / PS1XaPause (PauseSound: the pause menu, 1-up jingles): wait for the drive's PAUSE. Bounded (docs/ports/rsdk-common/29: every
// CD wait needs a bound; tester report on the v3 / v4 ports 2026-10-10, freezes at 1-ups not reproduced in emulators):
// after XA_WAIT_HBL without an answer the action is aborted (XaAction::abort), and after as long again it is given up
// (the drive answers nothing at all). Root counter 1 wraps at 16 bits: the elapsed time is summed per iteration.
#define XA_WAIT_HBL (3 * 15734) // ~3 s (NTSC hblanks; PAL a little longer)
void waitIdle() {
    uint32_t last = PS1Hblanks(), waited = 0;
    bool aborted = false;
    while (s_xa.running && g_ps1GPU) {
        g_ps1GPU->pumpCallbacks();
        uint32_t now = PS1Hblanks();
        waited += (now - last) & 0xffff;
        last = now;
        if (waited > XA_WAIT_HBL) {
            waited = 0;
            if (!aborted) {
                aborted = true;
                g_ps1XaAborts = g_ps1XaAborts + 1;
                s_action.abort();
            } else {
                g_ps1XaGiveUps = g_ps1XaGiveUps + 1;
                s_xa.running = false; // nothing answers: the next CD action may find the device busy
                break;
            }
        }
    }
}

void startAction(bool resume) {
    PS1FsStopStream();
    s_xa.stopping = false;
    s_xa.running = true;
    s_action.clearPauseRequest();
    s_action.clearAbort();
    eastl::atomic_signal_fence(eastl::memory_order_release);
    s_action.start(resume);
}


} // namespace

bool PS1XaInit() {
    uint32_t size = 0;
    if (!PS1FsFind("Data/Music/Music.xa", &s_fileLba, &size))
        return false;
    FILE *f = fopen("Data/Music/Music.bin", "rb");
    if (!f)
        return false;
    uint8_t head[12];
    if (fread(head, 1, 12, f) != 12 || memcmp(head, "XAM5", 4) || (head[6] | (head[7] << 8)) != XA_INTERLEAVE) {
        fclose(f);
        return false;
    }
    int count    = head[4] | (head[5] << 8);
    s_trackCount = 0;
    if (count > XA_MAX_TRACK)
        count = XA_MAX_TRACK;
    s_trackCount = (int)(fread(s_tracks, sizeof(Track), count, f));
    fclose(f);
    // psyqo's reset never demutes the CD audio output (CD-DA / XA).
    static volatile bool s_demuted;
    s_demuted = false;
    PS1FsStopStream();
    s_demute.start(&s_demuted);
    while (!s_demuted)
        g_ps1GPU->pumpCallbacks();
    s_ready = true;
    return true;
}

int PS1XaFindTrack(const char *name) {
    const char *prefix = "Data/Music/";
    size_t n           = strlen(prefix);
    if (!strncmp(name, prefix, n))
        name += n;
    uint32_t h = 0x811C9DC5u; // FNV-1a of the lower-case name (build_music.fnv1a)
    for (; *name; ++name)
        h = (h ^ (uint8_t)(*name >= 'A' && *name <= 'Z' ? *name + 32 : *name)) * 0x01000193u;
    for (int i = 0; i < s_trackCount; ++i)
        if (s_tracks[i].hash == h)
            return i;
    return -1;
}

void PS1XaPlay(int track, bool loop) { PS1XaPlayAt(track, loop, 0, 0); }

void PS1XaPlayAt(int track, bool loop, uint32_t row, uint32_t loopRow) {
    if (!s_ready || !g_ps1CDRom || track < 0 || track >= s_trackCount)
        return;
    if (s_xa.running) { // 9f: the drive is still busy: stop it without waiting, start this one when it is idle
        PS1XaStopAsync();
        s_xa.pend = { true, track, loop, row, loopRow };
        return;
    }
    s_xa.pend.on = false;
    PS1XaStop();
    s_xa.track   = track;
    s_xa.channel = s_tracks[track].channel;
    s_xa.loop    = loop;
    s_xa.ended   = false;
    s_xa.paused  = false;
    s_xa.offset  = row < s_tracks[track].rows ? row : 0;
    s_xa.loopRow = loopRow < s_tracks[track].rows ? loopRow : 0;
    s_xa.posBase = s_xa.offset;
    s_xa.posT0   = nowUs();
    s_xa.gapT0   = PS1Hblanks(); // a start's Setloc+ReadS gap reads like a loop's
    s_xa.seamMode = 0;
    g_ps1XaStarts     = g_ps1XaStarts + 1;
    g_ps1XaStartFrame = g_ps1PerfFrames;
    g_ps1XaChannel    = s_xa.channel;
    g_ps1XaTrack      = track;
    startAction(false);
}

void PS1XaStop() {
    s_xa.pend.on = false;
    if (s_xa.running) {
        if (!s_xa.stopping)
            s_action.requestPause();
        waitIdle();
    }
    s_xa.stopping = false;
    s_xa.paused   = false;
    s_xa.seamMode = 0; // 63 M8: a stop releases the seam, so the next owner (an ending's STR) starts at full volume
}

void PS1XaStopAsync() {
    s_xa.pend.on = false;
    if (s_xa.running && !s_xa.stopping) {
        s_action.requestPause();
        s_xa.stopping = true;
    }
    s_xa.paused   = false;
    s_xa.seamMode = 0;
}

void PS1XaService() {
    if (s_xa.running)
        return;
    s_xa.stopping = false;
    if (s_xa.pend.on) {
        s_xa.pend.on = false;
        PS1XaPlayAt(s_xa.pend.track, s_xa.pend.loop, s_xa.pend.row, s_xa.pend.loopRow);
    }
}

void PS1XaPause() {
    if (s_xa.running && !s_xa.ended) {
        s_xa.posBase = PS1XaPositionRows();
        s_action.requestPause();
        waitIdle();
        s_xa.paused = true;
    }
}

void PS1XaResume() {
    if (s_xa.paused && !s_xa.running) {
        s_xa.paused = false;
        s_xa.posT0  = nowUs();
        startAction(true);
    }
}

uint32_t PS1XaPositionRows() {
    if (s_xa.track < 0)
        return 0;
    uint32_t rows = s_xa.posBase;
    if (s_xa.running && !s_xa.paused && !s_xa.ended)
        rows += (nowUs() - s_xa.posT0) / 1000 * 3 / 160; // 18.75 rows per second (32-bit: no libgcc 64-bit divide)
    uint32_t n = s_tracks[s_xa.track].rows, from = s_xa.loopRow;
    return n && rows >= n ? from + (rows - n) % (n - from) : rows; // past the end: the loop part again
}

// docs/ports/mania/63 M8: the loop is a physical seam. The end marker's own row carries no audio for this channel
// (53 ms), the drive then Setlocs back to the loop row, and the ADPCM history restarts there: cutting the CD input at
// those edges is a click (the reported peak). Fade it instead, timed by the events that bound the seam -- the marker
// (where the drive has just read the track's last audio row) and the loop's ReadS ack (where the drive reads again):
// fade out over one row, hold through the driver's refill, fade in over one row. The CD input is sampled at playback,
// so scaling it during the fade scales the tail already in flight, which a position-derived fade cannot time: the
// drive reads ahead of what is heard.
#define SEAM_HBL      800 // one XA row (2,016 samples at 37.8 kHz = 53 ms = 800 hblanks at 15.7 kHz)
#define SEAM_HOLD_HBL 200 // ~1/4 row: the drive's refill after its ReadS ack (the fade-in starts just before the
                          // loop row can be heard; starting early only attenuates the first samples a little)
uint32_t PS1XaSeamGain() {
    if (g_ps1XaSeamOff)
        return g_ps1XaSeamGainLast = 0x7FFF;
    uint32_t mode = s_xa.seamMode, t = PS1HblanksSince(s_xa.seamT0);
    uint32_t gain = 0x7FFF;
    if (mode == 1) { // out
        if (t >= SEAM_HBL)
            gain = 0, s_xa.seamMode = 2, s_xa.seamT0 = PS1Hblanks();
        else
            gain = 0x7FFF - 0x7FFF * t / SEAM_HBL;
    }
    else if (mode == 2) { // hold (the drive's refill)
        if (t >= SEAM_HOLD_HBL) {
            s_xa.seamMode = 3;
            s_xa.seamT0   = PS1Hblanks();
        }
        gain = 0;
    }
    else if (mode == 3) { // in
        if (t >= SEAM_HBL)
            gain = 0x7FFF, s_xa.seamMode = 0;
        else
            gain = 0x7FFF * t / SEAM_HBL;
    }
    g_ps1XaSeamMode     = mode;
    return g_ps1XaSeamGainLast = gain;
}

uint32_t PS1XaTrackRows(int track) { return track >= 0 && track < s_trackCount ? s_tracks[track].rows : 0; }
uint32_t PS1XaTrackPad(int track) { return track >= 0 && track < s_trackCount ? s_tracks[track].pad : 0; }
int PS1XaCurrentTrack() { return s_xa.track; }
bool PS1XaCurrentLoop() { return s_xa.loop; }

bool PS1XaPlaying() { return (s_xa.running && !s_xa.ended && !s_xa.stopping) || s_xa.pend.on; }

// A queued start is not ready for the Time Attack menu. Wait for the drive to accept ReadS.
extern "C" int32_t PS1XaReadingTrack(const char *name) {
    return s_xa.running && !s_xa.stopping && !s_xa.ended && s_action.reading() && s_xa.track == PS1XaFindTrack(name);
}

bool PS1XaEnded() { return s_xa.ended && !s_xa.pend.on && !s_xa.stopping; } // (9f: not the old track a queued start replaces)

// cdrom_fs safety net: a file read while music plays (one CD action at a time).
bool PS1XaStopForFileRead() {
    if (!s_xa.running)
        return false;
    g_ps1XaForcedStops = g_ps1XaForcedStops + 1;
    PS1XaStop();
    return true;
}
