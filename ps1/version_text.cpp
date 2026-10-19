// Build stamp, resident part (docs/32 "Build stamp"): the git commit of the build (VERSION.TXT at the disc root, written
// by tools/disc/build_disc.py with tools/gen_version.py), read once at boot; the title's overlay draws it bottom-left
// (ps1/version_stamp.cpp, gen_overlays EXTRAS 'stamp': its PS1VersionLink sets g_ps1VersionDraw, which ps1/overlay.cpp
// resets at every overlay load). "v a1b2c3d4", "v a1b2c3d4-dirty", "v unknown" without the file.
#include <stdint.h>

#include <psyqo/cdrom-device.hh>
#include <psyqo/gpu.hh>

extern psyqo::GPU *g_ps1GPU;
extern psyqo::CDRomDevice *g_ps1CDRom;
bool PS1FsFind(const char *path, uint32_t *lba, uint32_t *size); // cdrom_fs.cpp

extern "C" {
char g_ps1VersionText[24] = "v unknown";
volatile uint32_t g_ps1VersionFound = 0; // 1 = VERSION.TXT was read
// the title overlay's draw (NULL elsewhere), called at the frame's end with the frame's FillScreen fade (colour 0xRRGGBB,
// alphas 0-255; 0 = none): RSDK/Graphics/PS1/PS1RenderDevice.cpp PS1RenderEndFrame
void (*g_ps1VersionDraw)(uint32_t colour, int32_t aR, int32_t aG, int32_t aB) = 0;
uint32_t g_ps1VersionFade[4] = { 0, 0, 0, 0 }; // the frame's last PS1FadeScreen (RSDK/Scene/PS1/PS1Layers.cpp)
}

// Boot (before the first load): one sector into a stack buffer (no heap, no read-ahead window).
void PS1VersionLoad() {
    uint32_t lba = 0, size = 0;
    uint32_t sec[512]; // one sector, word aligned for the CD DMA
    if (!PS1FsFind("VERSION.TXT", &lba, &size) || !size || !g_ps1CDRom->readSectorsBlocking(lba, 1, sec, *g_ps1GPU))
        return;
    const char *p = (const char *)sec;
    int n         = 2; // after "v "
    for (uint32_t i = 0; i < size && i < 16 && p[i] > ' '; ++i) g_ps1VersionText[n++] = p[i];
    if (n > 2) {
        g_ps1VersionText[n] = 0;
        g_ps1VersionFound   = 1;
    }
}
