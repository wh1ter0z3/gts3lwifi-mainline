// SPDX-License-Identifier: MIT
/*
 * gralloc.<ro.hardware>: a gralloc0 module named "CrOS Gralloc" that only
 * answers CROS_GRALLOC_DRM_GET_BUFFER_INFO, the perform() call with which
 * Mesa's u_gralloc (its "cros" backend) learns a minigbm buffer's format,
 * modifier and planes. A Mesa built with the NDK has no IMapper backend; the
 * vendor's minigbm (GloDroid) has a mapper HAL but no gralloc0 module, so
 * without this Mesa can't import YUV buffers (video) at all.
 *
 * The layout is minigbm's cros_gralloc_handle with five fds (DRV_MAX_FDS):
 * the magic at byte 116 (what the vendor's libminigbm_gralloc_gd checks).
 * Fields after it changed between minigbm versions; the plane count comes
 * from the format instead.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include <android/log.h>
#include <hardware/gralloc.h>

#define TAG "gralloc-cros-info"

#define CROS_GRALLOC_DRM_GET_BUFFER_INFO 4
#define CROS_GRALLOC_MAGIC 0xABCDDCBAu

/* byte offsets in the handle, native_handle_t (12 bytes) included */
#define OFF_FDS 12
#define OFF_STRIDES 32
#define OFF_OFFSETS 48
#define OFF_FORMAT 92
#define OFF_MODIFIER 100
#define OFF_MAGIC 116
#define HANDLE_MIN_SIZE (OFF_MAGIC + 4)

struct cros_gralloc0_buffer_info {
	uint32_t drm_fourcc;
	int num_fds;
	int fds[4];
	uint64_t modifier;
	int offset[4];
	int stride[4];
};

#define FOURCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

static int planes_of(uint32_t f)
{
	switch (f) {
	case FOURCC('N', 'V', '1', '2'): case FOURCC('N', 'V', '2', '1'):
	case FOURCC('N', 'V', '1', '6'): case FOURCC('N', 'V', '6', '1'):
	case FOURCC('P', '0', '1', '0'):
		return 2;
	case FOURCC('Y', 'U', '1', '2'): case FOURCC('Y', 'V', '1', '2'):
	case FOURCC('Y', 'U', '1', '6'): case FOURCC('Y', 'V', '1', '6'):
		return 3;
	default:
		return 1;
	}
}

static uint32_t rd32(const void *h, int off)
{
	uint32_t v;

	memcpy(&v, (const char *)h + off, sizeof(v));
	return v;
}

static int buffer_info(buffer_handle_t h, struct cros_gralloc0_buffer_info *info)
{
	const native_handle_t *nh = h;
	int planes, i;

	if (!nh || !info || nh->numFds < 1 ||
	    (int)sizeof(native_handle_t) + 4 * (nh->numFds + nh->numInts) < HANDLE_MIN_SIZE ||
	    rd32(nh, OFF_MAGIC) != CROS_GRALLOC_MAGIC) {
		static int warned;
		if (!warned++)
			__android_log_print(ANDROID_LOG_ERROR, TAG, "not a minigbm handle (fds %d ints %d)",
					    nh ? nh->numFds : -1, nh ? nh->numInts : -1);
		return -EINVAL;
	}

	memset(info, 0, sizeof(*info));
	info->drm_fourcc = rd32(nh, OFF_FORMAT);
	memcpy(&info->modifier, (const char *)nh + OFF_MODIFIER, sizeof(info->modifier));
	planes = planes_of(info->drm_fourcc);
	info->num_fds = planes;
	for (i = 0; i < planes; i++) {
		int fd = (int)rd32(nh, OFF_FDS + 4 * i);

		/* minigbm gives each plane its own fd; reuse the first if not */
		info->fds[i] = i < nh->numFds && fd >= 0 ? fd : (int)rd32(nh, OFF_FDS);
		info->offset[i] = (int)rd32(nh, OFF_OFFSETS + 4 * i);
		info->stride[i] = (int)rd32(nh, OFF_STRIDES + 4 * i);
	}
	return 0;
}

static int perform(const struct gralloc_module_t *mod, int op, ...)
{
	va_list ap;
	int ret = -EINVAL;

	(void)mod;
	va_start(ap, op);
	if (op == CROS_GRALLOC_DRM_GET_BUFFER_INFO) {
		buffer_handle_t h = va_arg(ap, buffer_handle_t);
		struct cros_gralloc0_buffer_info *info = va_arg(ap, struct cros_gralloc0_buffer_info *);

		ret = buffer_info(h, info);
	}
	va_end(ap);
	return ret;
}

static int no_open(const struct hw_module_t *m, const char *id, struct hw_device_t **dev)
{
	(void)m; (void)id; (void)dev;
	return -ENODEV;
}

static struct hw_module_methods_t methods = { .open = no_open };

__attribute__((visibility("default"))) struct gralloc_module_t HAL_MODULE_INFO_SYM = {
	.common = {
		.tag = HARDWARE_MODULE_TAG,
		.module_api_version = GRALLOC_MODULE_API_VERSION_0_2,
		.hal_api_version = HARDWARE_HAL_API_VERSION,
		.id = GRALLOC_HARDWARE_MODULE_ID,
		.name = "CrOS Gralloc",
		.author = "Galaxy Tab S3 GSI",
		.methods = &methods,
	},
	.perform = perform,
};
