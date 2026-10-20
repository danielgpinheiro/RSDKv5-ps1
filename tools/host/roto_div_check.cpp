// docs/32 9u: PS1Roto.cpp's PS1DivShl16 against the int64 expression it replaces, (int32)(((int64)n << 16) / d), d > 0:
// edge cases plus 50,000,000 random pairs. c++ -O2 tools/host/roto_div_check.cpp. Exit 0 = identical.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
typedef int32_t int32; typedef uint32_t uint32; typedef int64_t int64;
inline int32 PS1DivShl16(int32 n, int32 d)
{
    uint32 an = n < 0 ? 0u - (uint32)n : (uint32)n, ud = (uint32)d;
    uint32 q = an / ud, r = an % ud;
    for (int32 i = 0; i < 16; ++i) {
        r <<= 1, q <<= 1;
        if (r >= ud)
            r -= ud, q |= 1;
    }
    return (int32)(n < 0 ? 0u - q : q);
}
static uint64_t s = 88172645463325252ull;
static uint64_t nx() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
int main() {
    long bad = 0, n = 0;
    auto test = [&](int32 a, int32 d) {
        ++n;
        int32 want = (int32)(((int64)a << 16) / d), got = PS1DivShl16(a, d);
        if (want != got && bad++ < 5) printf("n=%d d=%d want %d got %d\n", a, d, want, got);
    };
    const int32 edge[] = { 0, 1, -1, 2, -2, 32767, -32768, 32768, 65535, -65536, 0x7FFFFFFF, (int32)0x80000000, 0x40000000, -0x40000000 };
    for (int32 a : edge) for (int32 d : { 1, 2, 3, 7, 240, 32767, 32768, 65536, 0x7FFFFFFF }) test(a, d);
    for (long i = 0; i < 50000000; ++i) {
        uint64_t r = nx();
        int32 a = (int32)r >> ((r >> 32) & 31);
        int32 d = (int32)((r >> 37) & 0x7FFFFFFF) >> ((r >> 59) & 31);
        if (d <= 0) d = 1;
        test(a, d);
    }
    printf("%ld pairs, %ld differ\n", n, bad);
    return bad != 0;
}
