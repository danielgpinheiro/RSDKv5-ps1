# STG inventory at an exhaustion (docs/32 7b): every live block of DATASET_STG (its owner variable's symbol, its size) when
# PS1StorageExhausted stops a load; with tools/boot_test.sh's BOOT build: tools/play_test.sh tools/play/stg_inventory.gdb OUT
break PS1StorageExhausted
commands
silent
printf "L EXHAUSTED set=%d size=%d used=%u limit=%u entries=%u\n", $a0, $a1, RSDK::dataStorage[0].usedStorage * 4, RSDK::dataStorage[0].storageLimit, RSDK::dataStorage[0].entryCount
set $i = 0
while $i < RSDK::dataStorage[0].entryCount
  set $d = RSDK::dataStorage[0].storageEntries[$i]
  set $o = RSDK::dataStorage[0].dataEntries[$i]
  if $d && $d[-4]
    printf "L STG %3d size=%7u owner=%08x ", $i, $d[-1], $o
    info symbol $o
  end
  set $i = $i + 1
end
quit
end
continue
