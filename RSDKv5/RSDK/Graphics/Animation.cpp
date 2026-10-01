#include "RSDK/Core/RetroEngine.hpp"

using namespace RSDK;

#if RETRO_REV0U
#include "Legacy/AnimationLegacy.cpp"
#endif

#if RETRO_PLATFORM == RETRO_PS1
#include "PS1/PS1Atlas.cpp" // the scene's sprite atlas (docs/32 3.1c)
#endif

SpriteAnimation RSDK::spriteAnimationList[SPRFILE_COUNT];

uint16 RSDK::LoadSpriteAnimation(const char *filePath, uint8 scope)
{
    if (!scope || scope > SCOPE_STAGE)
        return -1;

    char fullFilePath[0x100];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Sprites/%s", filePath);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filePath, hash);

    for (int32 i = 0; i < SPRFILE_COUNT; ++i) {
        if (HASH_MATCH_MD5(spriteAnimationList[i].hash, hash))
            return i;
    }

    uint16 id = -1;
    for (id = 0; id < SPRFILE_COUNT; ++id) {
        if (spriteAnimationList[id].scope == SCOPE_NONE)
            break;
    }

    if (id >= SPRFILE_COUNT)
        return -1;

    char nameBuffer[0x8][0x20];
    uint8 sheetIDs[0x18];
    sheetIDs[0] = 0;

    FileInfo info;
    InitFileInfo(&info);
    if (LoadFile(&info, fullFilePath, FMODE_RB)) {
        uint32 sig = ReadInt32(&info, false);

        if (sig != RSDK_SIGNATURE_SPR) {
            CloseFile(&info);
            return -1;
        }

        SpriteAnimation *spr = &spriteAnimationList[id];
        spr->scope           = scope;
        memcpy(spr->hash, hash, 4 * sizeof(uint32));

        uint32 frameCount = ReadInt32(&info, false);
#if RETRO_PLATFORM == RETRO_PS1
        // 7b: the animations the scene's atlas places nowhere (Scene<N>.pda) store no frames: one placeholder for all
        const uint8 *drop  = PS1AtlasDropped(hash);
        uint16 dropN       = 0;
        const uint8 *dropL = NULL;
        uint32 fileFrames  = frameCount;
        if (drop) {
            uint16 kept;
            memcpy(&kept, drop + 16, 2);
            memcpy(&dropN, drop + 18, 2);
            dropL      = drop + 20;
            frameCount = kept + 1;
        }
        (void)fileFrames;
#endif
#if RETRO_PLATFORM != RETRO_PS1
        AllocateStorage((void **)&spr->frames, frameCount * sizeof(SpriteFrame), DATASET_STG, false);
#endif

        uint8 sheetCount = ReadInt8(&info);
        for (int32 s = 0; s < sheetCount; ++s) {
            ReadString(&info, fullFilePath);
            sheetIDs[s] = LoadSpriteSheet(fullFilePath, scope);
        }

        uint8 hitboxCount = ReadInt8(&info);
        for (int32 h = 0; h < hitboxCount; ++h) {
            ReadString(&info, nameBuffer[h]);
        }
#if RETRO_PLATFORM == RETRO_PS1
        // the frames, then each frame's hitboxes (Animation.hpp)
        Hitbox *hitboxList = NULL;
        AllocateStorage((void **)&spr->frames, frameCount * (sizeof(SpriteFrame) + hitboxCount * sizeof(Hitbox)), DATASET_STG, false);
#endif

        spr->animCount = ReadInt16(&info);
        AllocateStorage((void **)&spr->animations, spr->animCount * sizeof(SpriteAnimationEntry), DATASET_STG, false);
#if RETRO_PLATFORM == RETRO_PS1
        spr->ps1Frames      = (uint16)frameCount;
        spr->ps1Placeholder = 0xFFFF;
        if (drop) { // the placeholder: the last frame, blank, its hitboxes zero
            spr->ps1Placeholder = (uint16)(frameCount - 1);
            SpriteFrame *ph     = &spr->frames[frameCount - 1];
            memset(ph, 0, sizeof(SpriteFrame));
            ph->duration     = 1;
            ph->hitboxCount  = hitboxCount;
            Hitbox *phBoxes  = (Hitbox *)(spr->frames + frameCount) + (frameCount - 1) * hitboxCount;
            memset(phBoxes, 0, hitboxCount * sizeof(Hitbox));
            ph->hitboxOffset = (uint16)(((uint8 *)phBoxes - (uint8 *)ph) >> 1);
        }
        uint16 dropAt = 0; // the next dropped animation in the list
#endif

        int32 frameID = 0;
        for (int32 a = 0; a < spr->animCount; ++a) {
            SpriteAnimationEntry *animation = &spr->animations[a];
            ReadString(&info, textBuffer);
            GEN_HASH_MD5_BUFFER(textBuffer, animation->hash);

            animation->frameCount      = ReadInt16(&info);
            animation->frameListOffset = frameID;
            animation->animationSpeed  = ReadInt16(&info);
            animation->loopIndex       = ReadInt8(&info);
            animation->rotationStyle   = ReadInt8(&info);
#if RETRO_PLATFORM == RETRO_PS1
            uint16 dropAnim = 0xFFFF;
            if (dropAt < dropN)
                memcpy(&dropAnim, dropL + 4 * dropAt, 2);
            if (dropAnim == a) { // its frames skipped; it shows the placeholder
                ++dropAt;
                Seek_Cur(&info, animation->frameCount * (1 + 8 * 2 + hitboxCount * 4 * 2)); // (a frame: u8 sheet, 8 i16, hitboxes)
                animation->frameCount      = 1;
                animation->frameListOffset = spr->ps1Placeholder;
                continue;
            }
#endif

            for (int32 f = 0; f < animation->frameCount; ++f) {
                SpriteFrame *frame = &spr->frames[frameID++];

                frame->sheetID     = sheetIDs[ReadInt8(&info)];
                frame->duration    = ReadInt16(&info);
                frame->unicodeChar = ReadInt16(&info);
                frame->sprX        = ReadInt16(&info);
                frame->sprY        = ReadInt16(&info);
                frame->width       = ReadInt16(&info);
                frame->height      = ReadInt16(&info);
                frame->pivotX      = ReadInt16(&info);
                frame->pivotY      = ReadInt16(&info);

                frame->hitboxCount = hitboxCount;
#if RETRO_PLATFORM == RETRO_PS1
                hitboxList          = (Hitbox *)(spr->frames + frameCount) + (frameID - 1) * hitboxCount;
                frame->hitboxOffset = (uint16)(((uint8 *)hitboxList - (uint8 *)frame) >> 1);
                for (int32 h = 0; h < hitboxCount; ++h) {
                    hitboxList[h].left   = ReadInt16(&info);
                    hitboxList[h].top    = ReadInt16(&info);
                    hitboxList[h].right  = ReadInt16(&info);
                    hitboxList[h].bottom = ReadInt16(&info);
                }
#else
                for (int32 h = 0; h < hitboxCount; ++h) {
                    frame->hitboxes[h].left   = ReadInt16(&info);
                    frame->hitboxes[h].top    = ReadInt16(&info);
                    frame->hitboxes[h].right  = ReadInt16(&info);
                    frame->hitboxes[h].bottom = ReadInt16(&info);
                }
#endif
            }
        }

        CloseFile(&info);
#if RETRO_PLATFORM == RETRO_PS1
        PS1AtlasBind(id); // each frame's place in VRAM (docs/32 3.1c)
#endif

        return id;
    }

    return -1;
}

#if RETRO_PLATFORM == RETRO_PS1
Hitbox RSDK::ps1EmptyHitbox;
#endif

uint16 RSDK::CreateSpriteAnimation(const char *filename, uint32 frameCount, uint32 animCount, uint8 scope)
{
    if (!scope || scope > SCOPE_STAGE)
        return -1;

    char fullFilePath[0x100];
    sprintf_s(fullFilePath, sizeof(fullFilePath), "Data/Sprites/%s", filename);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5(filename, hash);

    for (int32 i = 0; i < SPRFILE_COUNT; ++i) {
        if (HASH_MATCH_MD5(spriteAnimationList[i].hash, hash)) {
            return i;
        }
    }

    uint16 id = -1;
    for (id = 0; id < SPRFILE_COUNT; ++id) {
        if (spriteAnimationList[id].scope == SCOPE_NONE)
            break;
    }

    if (id >= SPRFILE_COUNT)
        return -1;

    SpriteAnimation *spr = &spriteAnimationList[id];
    spr->scope           = scope;
    memcpy(spr->hash, hash, 4 * sizeof(uint32));

    AllocateStorage((void **)&spr->frames, sizeof(SpriteFrame) * MIN(frameCount, SPRITEFRAME_COUNT), DATASET_STG, true);
#if RETRO_PLATFORM == RETRO_PS1
    spr->ps1Frames      = (uint16)MIN(frameCount, SPRITEFRAME_COUNT);
    spr->ps1Placeholder = 0xFFFF;
    for (uint32 f = 0; f < MIN(frameCount, SPRITEFRAME_COUNT); ++f) spr->frames[f].ps1Attr = PS1_ATLAS_MISSING; // not in the atlas
#endif
    AllocateStorage((void **)&spr->animations, sizeof(SpriteAnimationEntry) * MIN(animCount, SPRITEANIM_COUNT), DATASET_STG, true);

    return id;
}

void RSDK::ProcessAnimation(Animator *animator)
{
    if (!animator || !animator->frames)
        return;

    animator->timer += animator->speed;

    if (animator->frames == (SpriteFrame *)1) { // model anim
        while (animator->timer > animator->frameDuration) {
            ++animator->frameID;

            animator->timer -= animator->frameDuration;
            if (animator->frameID >= animator->frameCount)
                animator->frameID = animator->loopIndex;
        }
    }
    else { // sprite anim
        while (animator->timer > animator->frameDuration) {
            ++animator->frameID;

            animator->timer -= animator->frameDuration;
            if (animator->frameID >= animator->frameCount)
                animator->frameID = animator->loopIndex;

            animator->frameDuration = animator->frames[animator->frameID].duration;
        }
    }
}

int32 RSDK::GetStringWidth(uint16 aniFrames, uint16 animID, String *string, int32 startIndex, int32 length, int32 spacing)
{
    if (aniFrames >= SPRFILE_COUNT || !string || !string->chars)
        return 0;

    SpriteAnimation *spr = &spriteAnimationList[aniFrames];
    if (animID < spr->animCount) {
        SpriteAnimationEntry *anim = &spr->animations[animID];

        startIndex = CLAMP(startIndex, 0, string->length - 1);

        if (length <= 0 || length > string->length)
            length = string->length;

        int32 w = 0;
        for (int32 c = startIndex; c < length; ++c) {
            int32 charFrame = string->chars[c];
            if (charFrame < anim->frameCount) {
                w += spr->frames[anim->frameListOffset + charFrame].width;
                if (c + 1 >= length)
                    return w;

                w += spacing;
            }
        }

        return w;
    }

    return 0;
}

void RSDK::SetSpriteString(uint16 aniFrames, uint16 animID, String *string)
{
    if (aniFrames >= SPRFILE_COUNT || !string)
        return;

    SpriteAnimation *spr = &spriteAnimationList[aniFrames];
    if (animID < spr->animCount) {
        SpriteAnimationEntry *anim = &spr->animations[animID];

        for (int32 c = 0; c < string->length; ++c) {
            int32 unicodeChar = string->chars[c];
            string->chars[c]  = -1;
            for (int32 f = 0; f < anim->frameCount; ++f) {
                if (spr->frames[anim->frameListOffset + f].unicodeChar == unicodeChar) {
                    string->chars[c] = f;
                    break;
                }
            }
        }
    }
}
