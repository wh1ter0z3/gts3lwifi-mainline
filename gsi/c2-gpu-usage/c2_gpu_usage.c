// SPDX-License-Identifier: Apache-2.0
/*
 * c2-gpu-usage: LD_PRELOAD for the Codec2 V4L2 service. Its decoder takes
 * output buffers from its own pool, allocated for the decoder and the CPU
 * only (it doesn't take the consumer usage CCodec passes on), and CCodec
 * attaches them to the app's surface. SurfaceFlinger draws a buffer without
 * GPU texture usage as black ("blacked out as buffer is not gpu readable"),
 * and composites everything with the GPU here: decoded video showed black.
 *
 * This adds GPU_TEXTURE to video decoder buffers where they are allocated:
 * the service's libcodec2_vndk calls C2AllocatorGralloc::Impl::
 * newGraphicAllocation through the PLT, and both the gralloc allocation and
 * the usage kept in the Codec2 handle (CCodec makes the surface's
 * GraphicBuffer from that) come from its usage argument. (The gralloc usage
 * conversion in it is inlined: hooking that, or the allocator, isn't enough.)
 */
#include <dlfcn.h>
#include <stdint.h>

#define USAGE_GPU_TEXTURE (1ULL << 8)
#define USAGE_VIDEO_DECODER (1ULL << 22)	/* passed through as is in C2 usage */

#define NEW_GRAPHIC_ALLOCATION \
	_ZN7android18C2AllocatorGralloc4Impl20newGraphicAllocationEjjjRK13C2MemoryUsagePNSt3__110shared_ptrI19C2GraphicAllocationEE
#define STR(s) #s
#define XSTR(s) STR(s)

struct c2_memory_usage {
	uint64_t expected;
};

typedef int (*new_graphic_allocation_fn)(void *self, uint32_t width, uint32_t height,
					 uint32_t format, const struct c2_memory_usage *usage,
					 void *allocation);

__attribute__((visibility("default")))
int NEW_GRAPHIC_ALLOCATION(void *self, uint32_t width, uint32_t height, uint32_t format,
			   const struct c2_memory_usage *usage, void *allocation)
{
	static new_graphic_allocation_fn real;
	struct c2_memory_usage u = *usage;

	if (!real)
		real = (new_graphic_allocation_fn)dlsym(RTLD_NEXT, XSTR(NEW_GRAPHIC_ALLOCATION));
	if (u.expected & USAGE_VIDEO_DECODER)
		u.expected |= USAGE_GPU_TEXTURE;
	return real(self, width, height, format, &u, allocation);
}
