#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {
  ANDROID_DLEXT_RESERVED_ADDRESS = 0x1,
  ANDROID_DLEXT_RESERVED_ADDRESS_HINT = 0x2,
  ANDROID_DLEXT_WRITE_RELRO = 0x4,
  ANDROID_DLEXT_USE_RELRO = 0x8,
  ANDROID_DLEXT_USE_LIBRARY_FD = 0x10,
  ANDROID_DLEXT_USE_LIBRARY_FD_OFFSET = 0x20,
  ANDROID_DLEXT_FORCE_LOAD = 0x40,
  ANDROID_DLEXT_FORCE_FIXED_VADDR = 0x80,
  ANDROID_DLEXT_LOAD_AT_FIXED_ADDRESS = 0x100,
  ANDROID_DLEXT_USE_NAMESPACE = 0x200,
  ANDROID_DLEXT_VALID_FLAG_BITS = 0x3ff,
};
struct android_namespace_t;
typedef struct {
  uint64_t flags;
  void*   reserved_addr;
  size_t  reserved_size;
  int     relro_fd;
  int     library_fd;
  off64_t library_fd_offset;
  struct android_namespace_t* library_namespace;
} android_dlextinfo;
void* android_dlopen_ext(const char* filename, int flag, const android_dlextinfo* extinfo);
#ifdef __cplusplus
}
#endif
