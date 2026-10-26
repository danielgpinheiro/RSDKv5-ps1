// PS1 port (docs/ports/mania/32): operator new that stops instead of returning NULL. psyqo's operator new (alloc.c) returns
// psyqo_malloc's NULL when the heap is full; a constructor then runs on address 0 and overwrites the BIOS kernel's
// RAM (found in 2.2: UserDBStorage, 2.2 MB stock). Linked with -Wl,--wrap=_Znwj,--wrap=_Znaj (Makefile).
#include <stdint.h>
#include <stddef.h>

extern "C" void *__real__Znwj(unsigned int size);
extern "C" void *__real__Znaj(unsigned int size);

volatile uint32_t g_ps1OutOfMemory = 0; // size of the allocation that failed (GDB)

// GDB: `break PS1OutOfMemory` (tools/boot_test_ds.sh). Never returns.
extern "C" __attribute__((noinline)) void PS1OutOfMemory(unsigned int size)
{
    g_ps1OutOfMemory = size;
    for (;;) {
    }
}

extern "C" void PS1NoteAlloc(void *p, size_t size); // ps1/libc/stdlib.cpp: the init reserve's high-water mark

extern "C" void *__wrap__Znwj(unsigned int size)
{
    void *p = __real__Znwj(size);
    if (!p)
        PS1OutOfMemory(size);
    PS1NoteAlloc(p, size);
    return p;
}

extern "C" void *__wrap__Znaj(unsigned int size)
{
    void *p = __real__Znaj(size);
    if (!p)
        PS1OutOfMemory(size);
    PS1NoteAlloc(p, size);
    return p;
}
