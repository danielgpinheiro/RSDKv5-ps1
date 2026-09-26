// PS1 port (docs/32): one input device = pad 1 through psyqo::AdvancedPad (ps1/rsdk5_main.cpp reads it and
// fills g_ps1PadButtons every frame; the test harness can inject buttons through g_ps1PadTestMask, as in the
// Sonic CD port). Mapping as Sonic CD: D-pad, Cross = A, Circle = B, Square = C, Triangle = X, Start, Select.
#pragma once

// Included inside namespace RSDK by Input.hpp, like the other backends.
namespace SKU
{
struct InputDevicePS1 : InputDevice {
    void UpdateInput();
    void ProcessInput(int32 controllerID);
    void CloseDevice();

    uint16 buttonMasks;
    uint16 prevButtonMasks;
    uint8 stateUp, stateDown, stateLeft, stateRight;
    uint8 stateA, stateB, stateC, stateX, stateY, stateZ, stateStart, stateSelect;
};

void InitPS1InputAPI();
InputDevicePS1 *InitPS1InputDevice(uint32 id);
} // namespace SKU

// C linkage: the same variables from inside namespace RSDK (this header) and from ps1/rsdk5_main.cpp.
// Bit i = the button in the order above (Up, Down, Left, Right, A, B, C, X, Y, Z, Start, Select).
extern "C" volatile uint16_t g_ps1PadButtons;  // read from the pad each frame (ps1/rsdk5_main.cpp)
extern "C" volatile uint16_t g_ps1PadTestMask; // GDB-injected buttons (test harness), ORed in
