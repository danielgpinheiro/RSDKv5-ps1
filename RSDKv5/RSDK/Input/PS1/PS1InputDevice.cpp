// PS1 port: pad 1 as an InputDevice (see PS1InputDevice.hpp). Included by Input.cpp like the other backends.
// Modelled on Keyboard/KBInputDevice.cpp: the device's UpdateInput samples the buttons, ProcessInput
// converts them to the engine's InputState edges.

extern "C" {
volatile uint16_t g_ps1PadButtons  = 0;
volatile uint16_t g_ps1PadTestMask = 0;
}

using namespace RSDK;

void SKU::InputDevicePS1::UpdateInput()
{
    this->prevButtonMasks = this->buttonMasks;
    this->buttonMasks     = g_ps1PadButtons | g_ps1PadTestMask;
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

SKU::InputDevicePS1 *SKU::InitPS1InputDevice(uint32 id)
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
    InitPS1InputDevice(id);
}
