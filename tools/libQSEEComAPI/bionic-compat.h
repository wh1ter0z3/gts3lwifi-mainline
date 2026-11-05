/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Minimal declarations to build QSEEComAPI.c as a bionic (Android) library
 * without the Android NDK: only what the file uses. Types match bionic
 * arm64 (LP64), ioctl numbers are the kernel's.
 */
#pragma once
typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef int int32_t;
#ifndef __cplusplus
#if __STDC_VERSION__ < 202311L
typedef _Bool bool;
#endif
#endif
#define true 1
#define false 0
#define NULL ((void *)0)
#define O_RDWR 2
#define O_CLOEXEC 02000000
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_FAILED ((void *)-1)
#define EINVAL 22
#define ENOMEM 12
#define ENOTSUP 95
#define EEXIST 17
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT 8
#define _IOC_SIZESHIFT 16
#define _IOC_DIRSHIFT 30
#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U
#define _IOC(dir,type,nr,size) (((dir) << _IOC_DIRSHIFT) | ((type) << _IOC_TYPESHIFT) | ((nr) << _IOC_NRSHIFT) | ((size) << _IOC_SIZESHIFT))
#define _IO(type,nr) _IOC(_IOC_NONE,(type),(nr),0)
#define _IOW(type,nr,size) _IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#define _IOWR(type,nr,size) _IOC(_IOC_READ|_IOC_WRITE,(type),(nr),sizeof(size))
struct dma_heap_allocation_data { uint64_t len; uint32_t fd; uint32_t fd_flags; uint64_t heap_flags; };
#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
int open(const char *, int, ...);
long syscall(long, ...);
#define getdents64(fd, buf, n) syscall(61 /* __NR_getdents64 arm64 */, (long)(fd), (buf), (long)(n))
#define O_RDONLY 0
#define O_DIRECTORY 040000	/* arm64; 0x10000 is O_DIRECT */
int close(int);
int ioctl(int, unsigned long, ...);
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
void *calloc(size_t, size_t);
void free(void *);
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
char *strncpy(char *, const char *, size_t);
int strncmp(const char *, const char *, size_t);
size_t strlen(const char *);
char *strerror(int);
char *getenv(const char *);
volatile int *__errno(void);
#define errno (*__errno())
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#define ANDROID_LOG_INFO 4
