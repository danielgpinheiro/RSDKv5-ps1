"""Build-time pixel-art overrides for the PS1 port (docs/ports/mania/63-mania-20-fix-items-execution-plan.md M16).

The game data only has a DualShock 4 drawing for Options > Controls (UI/Picture.bin anim "Controllers" frame 0,
UI/Controllers.gif rect 1,1 137x87). The PS1 pad is a DualShock 1, so the drawing is edited deterministically here,
at build time, before the atlas packs it:

  * the touchpad area and its outline (absolute 44..89 x 4..28) are filled with the drawing's own body colour
    (palette index 176): the DS1 has no touchpad, and this also removes the light bar along its top edge;
  * the two SHARE / OPTIONS buttons beside it (37..42 and 93..98 x 6..15) are filled the same way: the DS1's
    SELECT / START are on the pad's centre, which the drawing already has.

Only the DS4 frame's rect is edited and only with indices the drawing already uses, so every other Picture frame stays
byte-identical and no new palette entry (which the menu's palette would recolour anyway) is introduced. The rules are
rectangles over the drawing's own pixels, not a new image, so the edit follows the source sheet if it is ever
re-extracted.
"""
import numpy as np

SHEET = 'UI/Controllers.gif'      # Picture.bin's sheet 1 ("Controllers")
DS4_RECT = (1, 1, 137, 87)        # x, y, w, h (Picture.bin anim 1 frame 0)
TOUCHPAD = (44, 4, 90, 29)        # absolute x0, y0, x1, y1 (exclusive ends; includes the outline)
SHARE_OPTIONS = ((37, 6, 42, 15), (93, 6, 98, 15))
BODY = 176                        # the drawing's body colour; the menu's palette tints it on screen


def pad_frame(im):
    """Edit the DS4 frame of a PIL P-mode sheet into a DualShock 1, erasing its touchpad and SHARE/OPTIONS buttons."""
    assert im.mode == 'P', im.mode
    px = np.array(im).copy()
    x, y, w, h = DS4_RECT
    for x0, y0, x1, y1 in (TOUCHPAD,) + SHARE_OPTIONS:
        assert x <= x0 < x1 <= x + w and y <= y0 < y1 <= y + h
        px[y0:y1, x0:x1] = BODY
    out = im.copy()
    out.putdata(px.flatten().tolist())
    return out
