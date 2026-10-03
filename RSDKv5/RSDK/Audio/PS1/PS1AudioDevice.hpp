// PS1 port (docs/32): the AudioDevice. Phase 2 = silent: the engine's channel bookkeeping runs (the game
// reads channel states), nothing plays. Phase 5 puts SFX on the SPU (ps1/spu) and music on CD-XA
// (ps1/xa_music), as Sonic CD; there is no software mixer, no Ogg and no WAV in RAM on the PS1.
#pragma once

#define LockAudioDevice()
#define UnlockAudioDevice()

namespace RSDK
{
void PS1SfxLoadBank(const char *folder, const char *id); // docs/32 5.1: the scene's SFX bank into SPU RAM
void PS1StreamStart(int32 channel); // docs/32 5.2: PlayStream's track on the CD-XA
void PS1StreamQueue(int32 channel); // 6.2: from LoadStream: started at the next FrameInit unless a video takes it
void PS1AudioStartPending();        // 9b.2: the queued stream started now (also before the draw lists)
const char *PS1StreamPendingName(); // 6.2: the last PlayStream's file (the video lookup)
void PS1StreamVideoDone();          // 6.2: the video ended: its stream channel with it

class AudioDevice : public AudioDeviceBase
{
public:
    static bool32 Init();
    static void Release();
    static void FrameInit(); // phase 2: advances the channels one frame (what the mixer does on the PC)
    inline static void HandleStreamLoad(ChannelInfo *channel, bool32 async) { RSDK::LoadStream(channel); }

private:
    static void InitAudioChannels();
};
} // namespace RSDK
