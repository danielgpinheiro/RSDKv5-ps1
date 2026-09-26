// PS1 port: silent AudioDevice (see PS1AudioDevice.hpp). Included by Audio.cpp like the other backends.

bool32 AudioDevice::Init()
{
    InitAudioChannels();
    return true;
}

void AudioDevice::Release() {}

void AudioDevice::InitAudioChannels() { AudioDeviceBase::InitAudioChannels(); }

// Once per engine frame (RunRetroEngine order): each playing channel advances by one frame of 44.1 kHz samples at
// its speed; an SFX past its length loops or ends exactly as in AudioDeviceBase::ProcessAudioMixing, so the game's
// IsSfxPlaying / GetChannelPos see the PC's state. SFX lengths are 0 until phase 5's table (they end after a frame).
void AudioDevice::FrameInit()
{
    const uint32 frameSamples = AUDIO_FREQUENCY / 60;
    for (int32 c = 0; c < CHANNEL_COUNT; ++c) {
        ChannelInfo *channel = &channels[c];
        if (channel->state == CHANNEL_SFX) {
            channel->bufferPos += (uint32)(((uint64)frameSamples * (uint32)channel->speed) >> 16);
            if (!channel->sampleLength) { // length unknown until phase 5: one-shots end, loops keep playing
                channel->bufferPos = 0;
                if (channel->loop == (uint32)-1) {
                    channel->state   = CHANNEL_IDLE;
                    channel->soundID = -1;
                }
            }
            else if (channel->bufferPos >= channel->sampleLength) {
                if (channel->loop == (uint32)-1) {
                    channel->state   = CHANNEL_IDLE;
                    channel->soundID = -1;
                }
                else {
                    channel->bufferPos = (channel->bufferPos - (uint32)channel->sampleLength) % (uint32)channel->sampleLength;
                    channel->bufferPos += channel->loop;
                }
            }
        }
        else if (channel->state == CHANNEL_STREAM) {
            channel->bufferPos += frameSamples;
        }
    }
}
