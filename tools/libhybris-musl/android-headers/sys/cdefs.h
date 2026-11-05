/* glibc-only header libhybris expects; musl has none. The macros it uses,
 * plus the glibc-isms musl lacks (defined without pulling sys/types.h,
 * which collides with musl bits/stat.h when force-included). */
#pragma once
#ifdef __cplusplus
#define __BEGIN_DECLS extern "C" {
#define __END_DECLS }
#else
#define __BEGIN_DECLS
#define __END_DECLS
#endif
#define __CONCAT(x,y) x ## y
#define __STRING(x) #x
#define __P(args) args
#define __THROW
#define __nonnull(params)
#define __wur
#define __attribute_malloc__
#define __packed __attribute__((__packed__))
#define __INTRODUCED_IN(x)
#ifndef __BIONIC_FORTIFY
#define __BIONIC_FORTIFY 0
#endif
typedef long off64_t;
#define mmap64 mmap
#define pread64 pread
#define lseek64 lseek
#define ftruncate64 ftruncate
#ifndef R_AARCH64_IRELATIVE
#define R_AARCH64_IRELATIVE 1032
#endif
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP {{{1}}}
#endif
#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp) ({ long int _rc; do _rc = (long int)(exp); while (_rc == -1 && errno == EINTR); _rc; })
#endif
