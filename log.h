#ifndef LOG_H
#define LOG_H
#include <syslog.h>
#include <stdarg.h>

void open_log(int interface, int slog);
void ngtc_log_callback(void* ptr, int level, const char* fmt, va_list vl);
#ifdef __GNUC__
void ngtc_log(void *avcl, int level, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
#else
void ngtc_log(void *avcl, int level, const char *fmt, ...);
#endif

#endif
