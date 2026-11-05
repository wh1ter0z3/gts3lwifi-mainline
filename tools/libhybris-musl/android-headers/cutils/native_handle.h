#pragma once
#include <stdalign.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NATIVE_HANDLE_MAX_FDS 1024
#define NATIVE_HANDLE_MAX_INTS 1024
typedef struct native_handle {
    int version;
    int numFds;
    int numInts;
    int data[0];
} native_handle_t;
typedef const native_handle_t* buffer_handle_t;
int native_handle_close(const native_handle_t* h);
native_handle_t* native_handle_init(char* storage, int numFds, int numInts);
native_handle_t* native_handle_create(int numFds, int numInts);
native_handle_t* native_handle_clone(const native_handle_t* handle);
int native_handle_delete(native_handle_t* h);
#ifdef __cplusplus
}
#endif
