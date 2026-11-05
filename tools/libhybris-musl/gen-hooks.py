# Generate extra hooks for bionic libc symbols the Android 9 libs import that
# this hybris tree lacks: direct hooks for musl-provided names, shims for the rest.
import re
musl = set(open('/tmp/musl.txt').read().split())
missing = open('/tmp/missing.txt').read().split()
direct = [s for s in missing if s in musl and s not in ('stderr','stdin','stdout','in6addr_any','tzname','__stack_chk_guard')]
data_syms = [s for s in missing if s in ('stderr','stdin','stdout','in6addr_any','tzname','__stack_chk_guard')]
shims = [s for s in missing if s not in musl]
shim_src = r'''
/* hybris musl additions: shims for bionic-only symbols the Android 9 libs import */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <locale.h>
#include <errno.h>
static void *hm_memcpy_chk(void *d, const void *s, size_t n, size_t dl) { (void)dl; return memcpy(d, s, n); }
static void *hm_memmove_chk(void *d, const void *s, size_t n, size_t dl) { (void)dl; return memmove(d, s, n); }
static void *hm_memset_chk(void *d, int c, size_t n, size_t dl) { (void)dl; return memset(d, c, n); }
static char *hm_strcpy_chk(char *d, const char *s, size_t dl) { (void)dl; return strcpy(d, s); }
static char *hm_strcat_chk(char *d, const char *s, size_t dl) { (void)dl; return strcat(d, s); }
static char *hm_strncpy_chk(char *d, const char *s, size_t n, size_t dl) { (void)dl; return strncpy(d, s, n); }
static char *hm_strncpy_chk2(char *d, const char *s, size_t n, size_t dl, size_t sl) { (void)dl; (void)sl; return strncpy(d, s, n); }
static char *hm_strncat_chk(char *d, const char *s, size_t n, size_t dl) { (void)dl; return strncat(d, s, n); }
static size_t hm_strlcpy_chk(char *d, const char *s, size_t n, size_t dl) { (void)dl; size_t l = strlen(s); if (n) { size_t c = l >= n ? n - 1 : l; memcpy(d, s, c); d[c] = 0; } return l; }
static size_t hm_strlen_chk(const char *s, size_t sl) { (void)sl; return strlen(s); }
static char *hm_strchr_chk(const char *s, int c, size_t sl) { (void)sl; return strchr(s, c); }
static char *hm_strrchr_chk(const char *s, int c, size_t sl) { (void)sl; return strrchr(s, c); }
static int hm_vsprintf_chk(char *s, int f, size_t sl, const char *fmt, va_list ap) { (void)f; return vsnprintf(s, sl, fmt, ap); }
static int hm_vsnprintf_chk(char *s, size_t n, int f, size_t sl, const char *fmt, va_list ap) { (void)f; (void)sl; return vsnprintf(s, n, fmt, ap); }
static char *hm_fgets_chk(char *s, int n, FILE *f, size_t sl) { (void)sl; return fgets(s, n, f); }
static size_t hm_fread_chk(void *p, size_t sz, size_t n, FILE *f, size_t pl) { (void)pl; return fread(p, sz, n, f); }
static size_t hm_fwrite_chk(const void *p, size_t sz, size_t n, FILE *f, size_t pl) { (void)pl; return fwrite(p, sz, n, f); }
static ssize_t hm_read_chk(int fd, void *b, size_t n, size_t bl) { (void)bl; return read(fd, b, n); }
static ssize_t hm_write_chk(int fd, const void *b, size_t n, size_t bl) { (void)bl; return write(fd, b, n); }
static ssize_t hm_pread_chk(int fd, void *b, size_t n, off_t o, size_t bl) { (void)bl; return pread(fd, b, n, o); }
static ssize_t hm_recvfrom_chk(int fd, void *b, size_t n, size_t bl, int fl, struct sockaddr *a, socklen_t *al) { (void)bl; return recvfrom(fd, b, n, fl, a, al); }
static int hm_poll_chk(struct pollfd *f, nfds_t n, int t, size_t fl) { (void)fl; return poll(f, n, t); }
static int hm_FD_ISSET_chk(int fd, fd_set *s, size_t sl) { (void)sl; return FD_ISSET(fd, s); }
static void hm_FD_SET_chk(int fd, fd_set *s, size_t sl) { (void)sl; FD_SET(fd, s); }
static int hm_open_2(const char *p, int fl) { return open(p, fl); }
static int hm_openat_2(int d, const char *p, int fl) { return openat(d, p, fl); }
static void hm_assert(const char *file, int line, const char *msg) { fprintf(stderr, "%s:%d: assertion \"%s\" failed\n", file, line, msg); abort(); }
static const char *hm_gnu_basename(const char *p) { const char *s = strrchr(p, '/'); return s ? s + 1 : p; }
static int hm_register_atfork(void (*pre)(void), void (*par)(void), void (*chi)(void), void *dso) { (void)dso; return pthread_atfork(pre, par, chi); }
static int hm_cxa_thread_atexit_impl(void (*f)(void *), void *o, void *d) { (void)f; (void)o; (void)d; return 0; }
static unsigned hm_prop_area_serial(void) { return 0; }
static void hm_prop_read_callback(const void *pi, void (*cb)(void *, const char *, const char *, unsigned), void *cookie) { (void)pi; (void)cb; (void)cookie; }
static void hm_set_abort_message(const char *m) { if (m) fprintf(stderr, "abort: %s\n", m); }
static const char *hm_getprogname(void) { return "hybris"; }
static off_t hm_lseek64(int fd, off_t o, int w) { return lseek(fd, o, w); }
static long long hm_strtoll_l(const char *s, char **e, int b, locale_t l) { (void)l; return strtoll(s, e, b); }
static unsigned long long hm_strtoull_l(const char *s, char **e, int b, locale_t l) { (void)l; return strtoull(s, e, b); }
static int hm_tgkill(int tg, int t, int sig) { return syscall(SYS_tgkill, tg, t, sig); }
'''
shim_map = {
 '__memcpy_chk':'hm_memcpy_chk','__memmove_chk':'hm_memmove_chk','__memset_chk':'hm_memset_chk','__strcpy_chk':'hm_strcpy_chk',
 '__strcat_chk':'hm_strcat_chk','__strncpy_chk':'hm_strncpy_chk','__strncpy_chk2':'hm_strncpy_chk2','__strncat_chk':'hm_strncat_chk',
 '__strlcpy_chk':'hm_strlcpy_chk','__strlen_chk':'hm_strlen_chk','__strchr_chk':'hm_strchr_chk','__strrchr_chk':'hm_strrchr_chk',
 '__vsprintf_chk':'hm_vsprintf_chk','__vsnprintf_chk':'hm_vsnprintf_chk','__fgets_chk':'hm_fgets_chk','__fread_chk':'hm_fread_chk',
 '__fwrite_chk':'hm_fwrite_chk','__read_chk':'hm_read_chk','__write_chk':'hm_write_chk','__pread_chk':'hm_pread_chk',
 '__recvfrom_chk':'hm_recvfrom_chk','__poll_chk':'hm_poll_chk','__FD_ISSET_chk':'hm_FD_ISSET_chk','__FD_SET_chk':'hm_FD_SET_chk',
 '__open_2':'hm_open_2','__openat_2':'hm_openat_2','__assert':'hm_assert','__gnu_basename':'hm_gnu_basename',
 '__register_atfork':'hm_register_atfork','__cxa_thread_atexit_impl':'hm_cxa_thread_atexit_impl',
 '__system_property_area_serial':'hm_prop_area_serial','__system_property_read_callback':'hm_prop_read_callback',
 'android_set_abort_message':'hm_set_abort_message','getprogname':'hm_getprogname','lseek64':'hm_lseek64',
 'strtoll_l':'hm_strtoll_l','strtoull_l':'hm_strtoull_l','tgkill':'hm_tgkill',
}
unmapped = [s for s in shims if s not in shim_map]
decls = ''.join('extern void *%s;\n' % s if s in ('in6addr_any','tzname') else '' for s in data_syms)
table = '\n/* hybris musl additions: hooks for symbols the Android 9 libs import */\nstatic struct _hook hooks_musl_extra[] = {\n'
for s in direct: table += '    HOOK_DIRECT_NO_DEBUG(%s),\n' % s
for s in ('stderr','stdin','stdout'): table += '    {"%s", &%s, &%s},\n' % (s, s, s)
for s in ('in6addr_any','tzname'): table += '    {"%s", (void *)&%s, (void *)&%s},\n' % (s, s, s)
table += '    {"__stack_chk_guard", (void *)&__stack_chk_guard_hybris, (void *)&__stack_chk_guard_hybris},\n'
for s, f in shim_map.items(): table += '    {"%s", %s, %s},\n' % (s, f, f)
table += '};\n'
open('/tmp/hooks_musl_extra.inc','w').write(shim_src + 'static unsigned long __stack_chk_guard_hybris = 0x595e9fbd94fda766UL;\n' + table)
print('direct', len(direct), 'shims', len(shim_map), 'unmapped', unmapped)
