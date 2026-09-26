/*
 * PS1 port — minimal <time.h>.
 *
 * Only used by Math.cpp's srand(time(NULL)). The port has no RTC clock yet,
 * so time() returns 0 (deterministic seed — fine for a boot stub). A real
 * clock (psyqo Kernel ticks or the PS1 RTC) is a Phase 6 item.
 */
#ifndef PS1_TIME_H
#define PS1_TIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef long time_t;
typedef long clock_t;

struct tm {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
};

static inline time_t time(time_t *tloc) {
    if (tloc)
        *tloc = 0;
    return 0;
}

static inline clock_t clock(void) {
    return 0;
}

/* RSDKv5 UserStorage (save-database row times, Time Attack records). No RTC: every time is the epoch;
 * localtime gives 1970-01-01 00:00:00, mktime inverts it for that date only. */
static inline struct tm *localtime(const time_t *t) {
    static struct tm s_tm;
    (void)t;
    s_tm.tm_sec = s_tm.tm_min = s_tm.tm_hour = 0;
    s_tm.tm_mday = 1;
    s_tm.tm_mon = 0;
    s_tm.tm_year = 70;
    s_tm.tm_wday = 4;
    s_tm.tm_yday = 0;
    s_tm.tm_isdst = 0;
    return &s_tm;
}

static inline time_t mktime(struct tm *t) {
    return (time_t)(((t->tm_yday * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec);
}

static inline double difftime(time_t a, time_t b) { return (double)(a - b); }

/* Minimal strftime: copies the format with %Y %m %d %H %M %S and %% expanded. */
size_t strftime(char *s, size_t max, const char *format, const struct tm *t);

#ifdef __cplusplus
}
#endif

#endif /* PS1_TIME_H */
