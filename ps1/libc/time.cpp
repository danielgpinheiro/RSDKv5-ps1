// PS1 port: strftime for the minimal <time.h> (RSDKv5 UserStorage formats save-database row times).
#include <stdio.h>
#include <time.h>

extern "C" size_t strftime(char *s, size_t max, const char *format, const struct tm *t)
{
    size_t n = 0;
    if (!max)
        return 0;
    for (const char *f = format; *f && n + 1 < max; ++f) {
        if (*f != '%' || !f[1]) {
            s[n++] = *f;
            continue;
        }
        char tmp[8];
        int v = -1, w = 2;
        switch (*++f) {
            case 'Y': v = t->tm_year + 1900; w = 4; break;
            case 'y': v = t->tm_year % 100; break;
            case 'm': v = t->tm_mon + 1; break;
            case 'd': v = t->tm_mday; break;
            case 'H': v = t->tm_hour; break;
            case 'M': v = t->tm_min; break;
            case 'S': v = t->tm_sec; break;
            case '%': s[n++] = '%'; continue;
            default: continue; // unsupported specifier: dropped
        }
        snprintf(tmp, sizeof(tmp), w == 4 ? "%04d" : "%02d", v);
        for (char *c = tmp; *c && n + 1 < max; ++c) s[n++] = *c;
    }
    s[n] = 0;
    return n;
}
