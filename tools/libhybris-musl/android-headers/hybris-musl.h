#define _GNU_SOURCE 1
/* force-included after the system headers: glibc functions musl keeps in other headers */
#pragma once
#include <string.h>
#include <libgen.h>
#include <errno.h>
/* musl basename takes char*; bionic code passes const char* */
#ifdef __cplusplus
static inline const char *hybris_basename(const char *p) { const char *s = strrchr(p, (int)'/'); return s ? s + 1 : p; }
#define basename(p) hybris_basename(p)
#endif
/* glibc-only stdio/alloc/thread functions libhybris hooks; musl aliases or stubs */
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <wchar.h>
#include <pthread.h>
#include <time.h>
#ifndef __cplusplus
typedef fpos_t fpos64_t;
#define fopen64 fopen
#define freopen64 freopen
#define fseeko64 fseeko
#define ftello64 ftello
#define fgetpos64 fgetpos
#define fsetpos64 fsetpos
#define pvalloc valloc
static inline wchar_t *wmempcpy(wchar_t *d, const wchar_t *s, size_t n) { return wmemcpy(d, s, n) + n; }
struct mallinfo { int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
static inline struct mallinfo mallinfo(void) { struct mallinfo m = {0}; return m; }
extern char *program_invocation_name;
static inline int scandirat(int dfd, const char *dir, struct dirent ***nl, int (*sel)(const struct dirent *), int (*cmp)(const struct dirent **, const struct dirent **)) { (void)dfd; return scandir(dir, nl, sel, cmp); }
static inline int pthread_attr_setstackaddr(pthread_attr_t *a, void *addr) { (void)a; (void)addr; return 0; }
static inline int pthread_attr_getstackaddr(const pthread_attr_t *a, void **addr) { (void)a; *addr = NULL; return 0; }
static inline int pthread_rwlockattr_setkind_np(pthread_rwlockattr_t *a, int k) { (void)a; (void)k; return 0; }
static inline int pthread_rwlockattr_getkind_np(const pthread_rwlockattr_t *a, int *k) { (void)a; *k = 0; return 0; }
static inline int pthread_cond_clockwait(pthread_cond_t *c, pthread_mutex_t *m, clockid_t clk, const struct timespec *t) { (void)clk; return pthread_cond_timedwait(c, m, t); }
#endif
#ifndef __cplusplus
#include <locale.h>
#ifndef pthread_setname_np
int pthread_setname_np(pthread_t, const char *);
#endif
int pthread_getattr_np(pthread_t, pthread_attr_t *);
#endif
