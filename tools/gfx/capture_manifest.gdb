# Sprite manifests (docs/32 3.1): the sprite files and sheets each scene loads, in order. Run with tools/gfx/capture_manifests.sh
# (it sets $end_frame). Prints "SCENE <folder> <id>" at each LoadSceneAssets, then "SPR <path>" / "SHT <path>" per load.
break RSDK::LoadSceneAssets
commands
silent
printf "SCENE %s %s\n", RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].folder, RSDK::sceneInfo.listData[RSDK::sceneInfo.listPos].id
continue
end
break RSDK::LoadSpriteAnimation
commands
silent
printf "SPR %s\n", (char*)$a0
continue
end
break RSDK::LoadSpriteSheet
commands
silent
printf "SHT %s\n", (char*)$a0
continue
end
break perfTick if g_ps1PerfFrames == $end_frame
commands
silent
printf "DONE\n"
detach
quit
end
continue
