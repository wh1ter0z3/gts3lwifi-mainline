
/* hybris musl additions: shims for bionic-only symbols the Android 9 libs import */
#include <ctype.h>
#include <wctype.h>
#include <wchar.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/ptrace.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <netdb.h>
#include <netinet/in.h>
#include <semaphore.h>
#include <sched.h>
#include <time.h>
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
void __stack_chk_fail(void);
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
static unsigned long __stack_chk_guard_hybris = 0x595e9fbd94fda766UL;

/* hybris musl additions: hooks for symbols the Android 9 libs import */
static struct _hook hooks_musl_extra[] = {
    HOOK_DIRECT_NO_DEBUG(__ctype_get_mb_cur_max),
    HOOK_DIRECT_NO_DEBUG(__stack_chk_fail),
    HOOK_DIRECT_NO_DEBUG(alarm),
    HOOK_DIRECT_NO_DEBUG(atoi),
    HOOK_DIRECT_NO_DEBUG(atol),
    HOOK_DIRECT_NO_DEBUG(bind),
    HOOK_DIRECT_NO_DEBUG(bsearch),
    HOOK_DIRECT_NO_DEBUG(btowc),
    HOOK_DIRECT_NO_DEBUG(chmod),
    HOOK_DIRECT_NO_DEBUG(chown),
    HOOK_DIRECT_NO_DEBUG(clock_gettime),
    HOOK_DIRECT_NO_DEBUG(connect),
    HOOK_DIRECT_NO_DEBUG(dup),
    HOOK_DIRECT_NO_DEBUG(dup2),
    HOOK_DIRECT_NO_DEBUG(epoll_create),
    HOOK_DIRECT_NO_DEBUG(epoll_ctl),
    HOOK_DIRECT_NO_DEBUG(epoll_wait),
    HOOK_DIRECT_NO_DEBUG(eventfd),
    HOOK_DIRECT_NO_DEBUG(exit),
    HOOK_DIRECT_NO_DEBUG(fallocate),
    HOOK_DIRECT_NO_DEBUG(fchmod),
    HOOK_DIRECT_NO_DEBUG(fchown),
    HOOK_DIRECT_NO_DEBUG(fcntl),
    HOOK_DIRECT_NO_DEBUG(fstat),
    HOOK_DIRECT_NO_DEBUG(fstatat),
    HOOK_DIRECT_NO_DEBUG(fsync),
    HOOK_DIRECT_NO_DEBUG(ftruncate),
    HOOK_DIRECT_NO_DEBUG(gai_strerror),
    HOOK_DIRECT_NO_DEBUG(getegid),
    HOOK_DIRECT_NO_DEBUG(geteuid),
    HOOK_DIRECT_NO_DEBUG(getgid),
    HOOK_DIRECT_NO_DEBUG(getgroups),
    HOOK_DIRECT_NO_DEBUG(getpagesize),
    HOOK_DIRECT_NO_DEBUG(getpgid),
    HOOK_DIRECT_NO_DEBUG(getpriority),
    HOOK_DIRECT_NO_DEBUG(getrlimit),
    HOOK_DIRECT_NO_DEBUG(getsockname),
    HOOK_DIRECT_NO_DEBUG(getsockopt),
    HOOK_DIRECT_NO_DEBUG(gettimeofday),
    HOOK_DIRECT_NO_DEBUG(getuid),
    HOOK_DIRECT_NO_DEBUG(gmtime_r),
    HOOK_DIRECT_NO_DEBUG(ioctl),
    HOOK_DIRECT_NO_DEBUG(isalnum),
    HOOK_DIRECT_NO_DEBUG(isdigit_l),
    HOOK_DIRECT_NO_DEBUG(islower),
    HOOK_DIRECT_NO_DEBUG(islower_l),
    HOOK_DIRECT_NO_DEBUG(isprint),
    HOOK_DIRECT_NO_DEBUG(isspace),
    HOOK_DIRECT_NO_DEBUG(isupper),
    HOOK_DIRECT_NO_DEBUG(isupper_l),
    HOOK_DIRECT_NO_DEBUG(iswalpha_l),
    HOOK_DIRECT_NO_DEBUG(iswblank_l),
    HOOK_DIRECT_NO_DEBUG(iswcntrl_l),
    HOOK_DIRECT_NO_DEBUG(iswdigit_l),
    HOOK_DIRECT_NO_DEBUG(iswlower_l),
    HOOK_DIRECT_NO_DEBUG(iswprint_l),
    HOOK_DIRECT_NO_DEBUG(iswpunct_l),
    HOOK_DIRECT_NO_DEBUG(iswspace_l),
    HOOK_DIRECT_NO_DEBUG(iswupper_l),
    HOOK_DIRECT_NO_DEBUG(iswxdigit_l),
    HOOK_DIRECT_NO_DEBUG(isxdigit),
    HOOK_DIRECT_NO_DEBUG(isxdigit_l),
    HOOK_DIRECT_NO_DEBUG(kill),
    HOOK_DIRECT_NO_DEBUG(listen),
    HOOK_DIRECT_NO_DEBUG(lroundf),
    HOOK_DIRECT_NO_DEBUG(lseek),
    HOOK_DIRECT_NO_DEBUG(lstat),
    HOOK_DIRECT_NO_DEBUG(madvise),
    HOOK_DIRECT_NO_DEBUG(mbrlen),
    HOOK_DIRECT_NO_DEBUG(mbrtowc),
    HOOK_DIRECT_NO_DEBUG(mbsnrtowcs),
    HOOK_DIRECT_NO_DEBUG(mbsrtowcs),
    HOOK_DIRECT_NO_DEBUG(mbtowc),
    HOOK_DIRECT_NO_DEBUG(mkdir),
    HOOK_DIRECT_NO_DEBUG(mkdirat),
    HOOK_DIRECT_NO_DEBUG(mkdtemp),
    HOOK_DIRECT_NO_DEBUG(mkstemp),
    HOOK_DIRECT_NO_DEBUG(mktime),
    HOOK_DIRECT_NO_DEBUG(nanosleep),
    HOOK_DIRECT_NO_DEBUG(poll),
    HOOK_DIRECT_NO_DEBUG(pread),
    HOOK_DIRECT_NO_DEBUG(process_vm_readv),
    HOOK_DIRECT_NO_DEBUG(ptrace),
    HOOK_DIRECT_NO_DEBUG(putchar),
    HOOK_DIRECT_NO_DEBUG(qsort),
    HOOK_DIRECT_NO_DEBUG(raise),
    HOOK_DIRECT_NO_DEBUG(read),
    HOOK_DIRECT_NO_DEBUG(readlink),
    HOOK_DIRECT_NO_DEBUG(realpath),
    HOOK_DIRECT_NO_DEBUG(recvfrom),
    HOOK_DIRECT_NO_DEBUG(recvmsg),
    HOOK_DIRECT_NO_DEBUG(remove),
    HOOK_DIRECT_NO_DEBUG(rename),
    HOOK_DIRECT_NO_DEBUG(rmdir),
    HOOK_DIRECT_NO_DEBUG(sched_getcpu),
    HOOK_DIRECT_NO_DEBUG(sched_setaffinity),
    HOOK_DIRECT_NO_DEBUG(sched_yield),
    HOOK_DIRECT_NO_DEBUG(select),
    HOOK_DIRECT_NO_DEBUG(sem_destroy),
    HOOK_DIRECT_NO_DEBUG(sem_init),
    HOOK_DIRECT_NO_DEBUG(sem_post),
    HOOK_DIRECT_NO_DEBUG(sem_timedwait),
    HOOK_DIRECT_NO_DEBUG(sem_wait),
    HOOK_DIRECT_NO_DEBUG(sendmsg),
    HOOK_DIRECT_NO_DEBUG(sendto),
    HOOK_DIRECT_NO_DEBUG(setpriority),
    HOOK_DIRECT_NO_DEBUG(setsockopt),
    HOOK_DIRECT_NO_DEBUG(shutdown),
    HOOK_DIRECT_NO_DEBUG(sleep),
    HOOK_DIRECT_NO_DEBUG(socket),
    HOOK_DIRECT_NO_DEBUG(stat),
    HOOK_DIRECT_NO_DEBUG(statfs),
    HOOK_DIRECT_NO_DEBUG(strcasestr),
    HOOK_DIRECT_NO_DEBUG(strcoll_l),
    HOOK_DIRECT_NO_DEBUG(strftime_l),
    HOOK_DIRECT_NO_DEBUG(strptime),
    HOOK_DIRECT_NO_DEBUG(strtod_l),
    HOOK_DIRECT_NO_DEBUG(strtof),
    HOOK_DIRECT_NO_DEBUG(strtof_l),
    HOOK_DIRECT_NO_DEBUG(strtoimax),
    HOOK_DIRECT_NO_DEBUG(strtold),
    HOOK_DIRECT_NO_DEBUG(strtold_l),
    HOOK_DIRECT_NO_DEBUG(strtoll),
    HOOK_DIRECT_NO_DEBUG(strtoul),
    HOOK_DIRECT_NO_DEBUG(strtoull),
    HOOK_DIRECT_NO_DEBUG(strxfrm_l),
    HOOK_DIRECT_NO_DEBUG(syscall),
    HOOK_DIRECT_NO_DEBUG(time),
    HOOK_DIRECT_NO_DEBUG(tolower),
    HOOK_DIRECT_NO_DEBUG(tolower_l),
    HOOK_DIRECT_NO_DEBUG(toupper),
    HOOK_DIRECT_NO_DEBUG(toupper_l),
    HOOK_DIRECT_NO_DEBUG(towlower_l),
    HOOK_DIRECT_NO_DEBUG(towupper_l),
    HOOK_DIRECT_NO_DEBUG(tzset),
    HOOK_DIRECT_NO_DEBUG(unlink),
    HOOK_DIRECT_NO_DEBUG(unsetenv),
    HOOK_DIRECT_NO_DEBUG(usleep),
    HOOK_DIRECT_NO_DEBUG(vprintf),
    HOOK_DIRECT_NO_DEBUG(wcrtomb),
    HOOK_DIRECT_NO_DEBUG(wcscoll_l),
    HOOK_DIRECT_NO_DEBUG(wcslen),
    HOOK_DIRECT_NO_DEBUG(wcsnrtombs),
    HOOK_DIRECT_NO_DEBUG(wcstod),
    HOOK_DIRECT_NO_DEBUG(wcstof),
    HOOK_DIRECT_NO_DEBUG(wcstol),
    HOOK_DIRECT_NO_DEBUG(wcstold),
    HOOK_DIRECT_NO_DEBUG(wcstoll),
    HOOK_DIRECT_NO_DEBUG(wcstoul),
    HOOK_DIRECT_NO_DEBUG(wcstoull),
    HOOK_DIRECT_NO_DEBUG(wcsxfrm_l),
    HOOK_DIRECT_NO_DEBUG(wctob),
    HOOK_DIRECT_NO_DEBUG(write),
    {"stderr", &stderr, &stderr},
    {"stdin", &stdin, &stdin},
    {"stdout", &stdout, &stdout},
    {"in6addr_any", (void *)&in6addr_any, (void *)&in6addr_any},
    {"tzname", (void *)&tzname, (void *)&tzname},
    {"__stack_chk_guard", (void *)&__stack_chk_guard_hybris, (void *)&__stack_chk_guard_hybris},
    {"__memcpy_chk", hm_memcpy_chk, hm_memcpy_chk},
    {"__memmove_chk", hm_memmove_chk, hm_memmove_chk},
    {"__memset_chk", hm_memset_chk, hm_memset_chk},
    {"__strcpy_chk", hm_strcpy_chk, hm_strcpy_chk},
    {"__strcat_chk", hm_strcat_chk, hm_strcat_chk},
    {"__strncpy_chk", hm_strncpy_chk, hm_strncpy_chk},
    {"__strncpy_chk2", hm_strncpy_chk2, hm_strncpy_chk2},
    {"__strncat_chk", hm_strncat_chk, hm_strncat_chk},
    {"__strlcpy_chk", hm_strlcpy_chk, hm_strlcpy_chk},
    {"__strlen_chk", hm_strlen_chk, hm_strlen_chk},
    {"__strchr_chk", hm_strchr_chk, hm_strchr_chk},
    {"__strrchr_chk", hm_strrchr_chk, hm_strrchr_chk},
    {"__vsprintf_chk", hm_vsprintf_chk, hm_vsprintf_chk},
    {"__vsnprintf_chk", hm_vsnprintf_chk, hm_vsnprintf_chk},
    {"__fgets_chk", hm_fgets_chk, hm_fgets_chk},
    {"__fread_chk", hm_fread_chk, hm_fread_chk},
    {"__fwrite_chk", hm_fwrite_chk, hm_fwrite_chk},
    {"__read_chk", hm_read_chk, hm_read_chk},
    {"__write_chk", hm_write_chk, hm_write_chk},
    {"__pread_chk", hm_pread_chk, hm_pread_chk},
    {"__recvfrom_chk", hm_recvfrom_chk, hm_recvfrom_chk},
    {"__poll_chk", hm_poll_chk, hm_poll_chk},
    {"__FD_ISSET_chk", hm_FD_ISSET_chk, hm_FD_ISSET_chk},
    {"__FD_SET_chk", hm_FD_SET_chk, hm_FD_SET_chk},
    {"__open_2", hm_open_2, hm_open_2},
    {"__openat_2", hm_openat_2, hm_openat_2},
    {"__assert", hm_assert, hm_assert},
    {"__gnu_basename", hm_gnu_basename, hm_gnu_basename},
    {"__register_atfork", hm_register_atfork, hm_register_atfork},
    {"__cxa_thread_atexit_impl", hm_cxa_thread_atexit_impl, hm_cxa_thread_atexit_impl},
    {"__system_property_area_serial", hm_prop_area_serial, hm_prop_area_serial},
    {"__system_property_read_callback", hm_prop_read_callback, hm_prop_read_callback},
    {"android_set_abort_message", hm_set_abort_message, hm_set_abort_message},
    {"getprogname", hm_getprogname, hm_getprogname},
    {"lseek64", hm_lseek64, hm_lseek64},
    {"strtoll_l", hm_strtoll_l, hm_strtoll_l},
    {"strtoull_l", hm_strtoull_l, hm_strtoull_l},
    {"tgkill", hm_tgkill, hm_tgkill},
};
