/* PS1 port: <assert.h> shim. Assertions are compiled out (miniz, engine); the port uses its own GDB counters. */
#ifndef PS1_ASSERT_H
#define PS1_ASSERT_H
#define assert(x) ((void)0)
#endif
