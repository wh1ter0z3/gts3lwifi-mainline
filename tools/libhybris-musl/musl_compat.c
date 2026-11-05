/* glibc fortify entry points libhybris exports to Android code; musl has none. */
#include <stdarg.h>
#include <stdio.h>
int __vsprintf_chk(char *s, int flag, size_t slen, const char *fmt, va_list ap)
{ (void)flag; return vsnprintf(s, slen, fmt, ap); }
int __vsnprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, va_list ap)
{ (void)flag; (void)slen; return vsnprintf(s, maxlen, fmt, ap); }
int __sprintf_chk(char *s, int flag, size_t slen, const char *fmt, ...)
{ va_list ap; int r; (void)flag; va_start(ap, fmt); r = vsnprintf(s, slen, fmt, ap); va_end(ap); return r; }
int __snprintf_chk(char *s, size_t maxlen, int flag, size_t slen, const char *fmt, ...)
{ va_list ap; int r; (void)flag; (void)slen; va_start(ap, fmt); r = vsnprintf(s, maxlen, fmt, ap); va_end(ap); return r; }
