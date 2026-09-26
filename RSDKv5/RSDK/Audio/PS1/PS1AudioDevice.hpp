// PS1 port (docs/32): the AudioDevice. Phase 2 = silent: the engine's channel bookkeeping runs (the game
// reads channel states), nothing plays. Phase 5 puts SFX on the SPU (ps1/spu) and music on CD-XA
// (ps1/xa_music), as Sonic CD; there is no software mixer, no Ogg and no WAV in RAM on the PS1.
#pragma once

#define LockAudioDevice()
#define UnlockAudioDevice()

namespace RSDK
{
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
