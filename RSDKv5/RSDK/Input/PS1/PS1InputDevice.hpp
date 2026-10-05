// PS1 port (docs/32): one input device = pad 1 through psyqo::AdvancedPad (ps1/rsdk5_main.cpp reads it and
// fills g_ps1PadButtons every frame; the test harness can inject buttons through g_ps1PadTestMask, as in the
// Sonic CD port). Mapping (docs/32 9b.5, as the menus' prompts): D-pad, Cross = A, Circle = B, L1 = C, Square = X,
// Triangle = Y, R1 = Z, Start, Select.
#pragma once

// Included inside namespace RSDK by Input.hpp, like the other backends.
namespace SKU
{
struct InputDevicePS1 : InputDevice {
    void UpdateInput();
    void ProcessInput(int32 controllerID);
    void CloseDevice();

    // docs/32 4.2a: the one pad device in static storage (PS1InputDevice.cpp), not on the heap
    static void *operator new(size_t size);
    static void operator delete(void *ptr);

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

// docs/32 9b.5 (test harness, GDB): a scripted pad. Step k = g_ps1PadScript[4k .. 4k+3]: the scene (listPos; 0xFFFF any;
// 0xFFFE the next scene loaded after the previous step),
// the frames after that scene's load (g_ps1SceneLoadSerial), the buttons, the frames they are held; the steps run in
// order, g_ps1PadScriptSteps of them (0: off), g_ps1PadScriptStep = the next one.
extern "C" volatile uint16_t g_ps1PadScript[4 * 48];
extern "C" volatile uint32_t g_ps1PadScriptSteps, g_ps1PadScriptStep, g_ps1SceneLoadSerial;
