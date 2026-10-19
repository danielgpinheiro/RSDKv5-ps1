// PS1Pad: psyqo's AdvancedPad reduced to what the port reads (docs/32 9t, docs/47 item 5; from RSDKv4-ps1's ps1/pad.hh,
// docs/43 C11). AdvancedPad's Fast mode polled both ports every frame, and an empty port never sends /ACK: each frame
// waited out its whole timeout (~105 us) after one byte (nocash psx-spx "Controller and Memory Card I/O Ports"; "PSX
// expects No-connection if not returned Acknowledge less than 100 usec", "Controller and Memory Card Signals"). Mania
// reads pad 2 too (Puyo Puyo's 2P, and every pad feeds the menus' "any" controller: RSDK/Input/PS1/PS1InputDevice.cpp),
// so PS1Pad polls port 1 every frame and port 2 every frame while a pad answers there, else once every 32 frames (a pad
// plugged into port 2 shows up within ~0.5 s). AdvancedPad's own byte sequence, timings and multitap handling (copied
// from nugget/psyqo/src/advancedpad.cpp, MIT, PCSX-Redux authors; nugget itself stays upstream). Same API for the calls
// the port makes.
#pragma once
#include <stdint.h>
#include <psyqo/advancedpad.hh>

class PS1Pad {
  public:
    using Pad    = psyqo::AdvancedPad::Pad;
    using Button = psyqo::AdvancedPad::Button;
    void initialize();
    bool isPadConnected(Pad pad) const { return (unsigned)pad < 8 && m_padData[(unsigned)pad].connected == 0; }
    bool isButtonPressed(Pad pad, Button button) const {
        return (unsigned)pad < 8 && (m_padData[(unsigned)pad].buttons & (1 << (unsigned)button)) == 0;
    }

  private:
    void readPad();
    bool readPort(uint8_t port); // true when a device answered (its first ACK came)
    union PadData {
        struct {
            uint8_t connected;
            uint8_t padType;
            uint16_t buttons;
            uint8_t adc[8];
        };
        uint16_t packed[6];
    };
    PadData m_padData[8]; // ports 1 and 2: pads A-D each (a multitap), as AdvancedPad's Pad1a-Pad2d
    bool m_port2Live = false;
    uint8_t m_frame  = 0;
};
