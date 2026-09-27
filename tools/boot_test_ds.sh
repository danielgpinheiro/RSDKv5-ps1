#!/bin/bash
# boot_test.sh in DuckStation (retail BIOS): only when no other session uses it (2026-09-28: another session does).
EMU=ds exec "$(dirname "$0")/boot_test.sh" "$@"
