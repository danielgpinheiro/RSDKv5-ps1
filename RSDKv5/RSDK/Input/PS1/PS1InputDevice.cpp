// PS1 port: pad 1 as an InputDevice (see PS1InputDevice.hpp). Included by Input.cpp like the other backends.
// Modelled on Keyboard/KBInputDevice.cpp: the device's UpdateInput samples the buttons, ProcessInput
// converts them to the engine's InputState edges.

extern "C" {
volatile uint16_t g_ps1PadButtons  = 0;
volatile uint16_t g_ps1PadTestMask = 0;
volatile uint16_t g_ps1Pad2Buttons = 0, g_ps1Pad2TestMask = 0;
volatile uint16_t g_ps1PadScript[4 * 48];
volatile uint32_t g_ps1PadScriptSteps = 0, g_ps1PadScriptStep = 0, g_ps1SceneLoadSerial = 0;
}

// 9b.5: the scripted pad's buttons this frame (see PS1InputDevice.hpp)
static uint16 PS1PadScriptButtons()
{
    static uint32 serial = 0, frames = 0, stepSerial = 0, pressSerial = 0;
    static bool pressing = false;
    if (serial != ::g_ps1SceneLoadSerial)
        serial = ::g_ps1SceneLoadSerial, frames = 0;
    else
        ++frames;
    while (::g_ps1PadScriptStep < ::g_ps1PadScriptSteps) {
        volatile uint16_t *s = &::g_ps1PadScript[4 * ::g_ps1PadScriptStep];
        if (!(pressing && serial != pressSerial)) { // (a press its scene's end cut short is done)
            if (s[0] == 0xFFFE ? serial == stepSerial : s[0] != 0xFFFF && s[0] != RSDK::sceneInfo.listPos)
                return 0;
            if (frames < s[1])
                return 0;
            if (frames < (uint32)s[1] + s[3]) {
                pressing = true, pressSerial = serial;
                return s[2];
            }
        }
        ::g_ps1PadScriptStep = ::g_ps1PadScriptStep + 1;
        stepSerial           = serial;
        pressing             = false;
    }
    return 0;
}

using namespace RSDK;
#if PS1_DEVMENU
extern "C" volatile uint16_t g_ps1DevSwallow; // ps1/dev_menu.cpp
#endif

// docs/ports/mania/32 4.2a: created at init just above a CD read window that is freed afterwards, the heap device (56 bytes) split
// the hole the chain arena takes after each load: the menu (2 MB) found 32.8 + 22.8 KB and no 51 KB arena. The pads
// (9f: two, for Puyo Puyo's 2P), never removed on the PS1: static storage, and delete has nothing to free.
alignas(8) static uint8 s_ps1PadDeviceMem[2][sizeof(SKU::InputDevicePS1)];
static uint32 s_ps1PadDevices = 0;
void *SKU::InputDevicePS1::operator new(size_t size)
{
    if (size > sizeof(s_ps1PadDeviceMem[0]))
        return NULL;
    return s_ps1PadDeviceMem[s_ps1PadDevices < 2 ? s_ps1PadDevices++ : 1];
}
void SKU::InputDevicePS1::operator delete(void *ptr) {}

void SKU::InputDevicePS1::UpdateInput()
{
    this->prevButtonMasks = this->buttonMasks;
    if (this->port)
        this->buttonMasks = g_ps1Pad2Buttons | g_ps1Pad2TestMask;
    else
        this->buttonMasks = g_ps1PadButtons | g_ps1PadTestMask | (::g_ps1PadScriptSteps ? PS1PadScriptButtons() : 0);
#if PS1_DEVMENU
    if (!this->port)
        this->buttonMasks &= ~g_ps1DevSwallow; // the Select that toggled the dev menu, until released (ps1/dev_menu.cpp)
#endif
    uint16 changed        = this->buttonMasks ^ this->prevButtonMasks;

    this->inactiveTimer[0] = this->buttonMasks ? 0 : this->inactiveTimer[0] + 1;
    this->inactiveTimer[1] = this->buttonMasks ? 0 : this->inactiveTimer[1] + 1;
    this->anyPress         = this->buttonMasks != 0;

    uint8 *states[] = { &stateUp, &stateDown, &stateLeft, &stateRight, &stateA,     &stateB,
                        &stateC,  &stateX,    &stateY,    &stateZ,     &stateStart, &stateSelect };
    for (int32 i = 0; i < 12; ++i) {
        bool32 down = (this->buttonMasks >> i) & 1;
        if (down)
            *states[i] = (changed >> i) & 1 ? 1 : 2; // pressed this frame : held
        else
            *states[i] = (changed >> i) & 1 ? 3 : 0; // released this frame : up
    }

    // docs/ports/mania/32 4.2a: every stock device feeds the "any" controller here (KBInputDevice::UpdateInput); the menus read it
    // (UIControl's anyConfirmPress / anyBackPress: the autosave dialog never saw a press)
    this->ProcessInput(CONT_ANY);
}

void SKU::InputDevicePS1::ProcessInput(int32 controllerID)
{
    ControllerState *cont = &controller[controllerID];
    InputState *keys[]    = { &cont->keyUp, &cont->keyDown, &cont->keyLeft,  &cont->keyRight, &cont->keyA,     &cont->keyB,
                              &cont->keyC,  &cont->keyX,    &cont->keyY,     &cont->keyZ,     &cont->keyStart, &cont->keySelect };
    uint8 *states[]       = { &stateUp, &stateDown, &stateLeft, &stateRight, &stateA,     &stateB,
                              &stateC,  &stateX,    &stateY,    &stateZ,     &stateStart, &stateSelect };
    // As the keyboard device: a held button ORs `press`; Input.cpp's pass after the devices turns that into `down` and a
    // one-frame `press` (writing `down` here was undone by that pass: a held button read as released, 2.5).
    for (int32 i = 0; i < 12; ++i) keys[i]->press |= *states[i] == 1 || *states[i] == 2;
}

void SKU::InputDevicePS1::CloseDevice()
{
    this->active       = false;
    this->isAssigned   = false;
    this->buttonMasks  = 0;
    this->prevButtonMasks = 0;
}

SKU::InputDevicePS1 *SKU::InitPS1InputDevice(uint32 id, uint8 port)
{
    if (inputDeviceCount >= INPUTDEVICE_COUNT)
        return NULL;
    if (inputDeviceList[inputDeviceCount] && inputDeviceList[inputDeviceCount]->active)
        return NULL;
    if (inputDeviceList[inputDeviceCount])
        delete inputDeviceList[inputDeviceCount];
    inputDeviceList[inputDeviceCount] = new InputDevicePS1();
    InputDevicePS1 *device            = (InputDevicePS1 *)inputDeviceList[inputDeviceCount];
    if (!device)
        return NULL;
    device->gamepadType = (DEVICE_API_NONE << 16) | (DEVICE_TYPE_CONTROLLER << 8) | (DEVICE_PS4 << 0);
    device->port        = port;
    device->disabled    = false;
    device->id          = id;
    device->active      = true;
    for (int32 i = 0; i < PLAYER_COUNT; ++i) {
        if (inputSlots[i] == id) {
            inputSlotDevices[i] = device;
            device->isAssigned  = true;
        }
    }
    inputDeviceCount++;
    return device;
}

void SKU::InitPS1InputAPI()
{
    char idString[] = "PS1Pad1";
    uint32 id       = 0;
    GenerateHashCRC(&id, idString);
    InitPS1InputDevice(id, 0);
    // 9f: pad 2 (Puyo Puyo's 2P versus): a second device the game assigns like any other (its presses go to "any" too)
    char idString2[] = "PS1Pad2";
    uint32 id2       = 0;
    GenerateHashCRC(&id2, idString2);
    InitPS1InputDevice(id2, 1);
}
