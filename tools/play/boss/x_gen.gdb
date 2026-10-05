add-symbol-file @OVL@
break RSDK::PS1EntityRelocate
commands
silent
printf "L RELOC f=%u slot=%d size=%u\n", g_ps1PerfFrames, $a0, $a1
bt 6
continue
end
