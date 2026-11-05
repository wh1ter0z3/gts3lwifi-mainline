// SPDX-License-Identifier: BSD-3-Clause
/*
 * Open QSEEComAPI for mainline Linux: the client API of the vendor
 * libQSEEComAPI.so on /dev/qseecom (downstream ioctl ABI), with the shared
 * buffers from a DMA-BUF heap instead of ION. The handle layout and the
 * call order follow the vendor library, so that the HALs and qseecomd
 * work unchanged.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-heap.h>

#include "QSEEComAPI.h"

#define QSEECOM_DEV	"/dev/qseecom"
#define HEAP_DEV	(getenv("QSEECOM_HEAP") ? getenv("QSEECOM_HEAP") : "/dev/dma_heap/qseecom@fec00000")
#define LOG(fmt, ...)	do { if (getenv("QSEECOM_DEBUG")) fprintf(stderr, "QSEEComAPI: " fmt "\n", ##__VA_ARGS__); } while (0)

/* downstream ioctl ABI (include/uapi/linux/qseecom.h) */
#define QSEECOM_IOC_MAGIC 0x97
struct qseecom_register_listener_req { uint32_t listener_id; int32_t ifd_data_fd; void *virt_sb_base; uint32_t sb_size; };
struct qseecom_send_cmd_req { void *cmd_req_buf; unsigned int cmd_req_len; void *resp_buf; unsigned int resp_len; };
struct qseecom_load_img_req { uint32_t mdt_len; uint32_t img_len; int32_t ifd_data_fd; char img_name[64]; uint32_t app_arch; uint32_t app_id; };
struct qseecom_set_sb_mem_param_req { int32_t ifd_data_fd; void *virt_sb_base; uint32_t sb_len; };
struct qseecom_qseos_version_req { unsigned int qseos_version; };
struct qseecom_qseos_app_load_query { char app_name[64]; uint32_t app_id; uint32_t app_arch; };
#define QSEECOM_IOCTL_REGISTER_LISTENER_REQ	_IOWR(QSEECOM_IOC_MAGIC, 1, struct qseecom_register_listener_req)
#define QSEECOM_IOCTL_UNREGISTER_LISTENER_REQ	_IO(QSEECOM_IOC_MAGIC, 2)
#define QSEECOM_IOCTL_SEND_CMD_REQ		_IOWR(QSEECOM_IOC_MAGIC, 3, struct qseecom_send_cmd_req)
#define QSEECOM_IOCTL_RECEIVE_REQ		_IO(QSEECOM_IOC_MAGIC, 5)
#define QSEECOM_IOCTL_SEND_RESP_REQ		_IO(QSEECOM_IOC_MAGIC, 6)
#define QSEECOM_IOCTL_LOAD_APP_REQ		_IOWR(QSEECOM_IOC_MAGIC, 7, struct qseecom_load_img_req)
#define QSEECOM_IOCTL_SET_MEM_PARAM_REQ		_IOWR(QSEECOM_IOC_MAGIC, 8, struct qseecom_set_sb_mem_param_req)
#define QSEECOM_IOCTL_UNLOAD_APP_REQ		_IO(QSEECOM_IOC_MAGIC, 9)
#define QSEECOM_IOCTL_GET_QSEOS_VERSION_REQ	_IOWR(QSEECOM_IOC_MAGIC, 10, struct qseecom_qseos_version_req)
#define QSEECOM_IOCTL_PERF_ENABLE_REQ		_IO(QSEECOM_IOC_MAGIC, 11)
#define QSEECOM_IOCTL_PERF_DISABLE_REQ		_IO(QSEECOM_IOC_MAGIC, 12)
#define QSEECOM_IOCTL_APP_LOADED_QUERY_REQ	_IOWR(QSEECOM_IOC_MAGIC, 15, struct qseecom_qseos_app_load_query)
#define QSEECOM_IOCTL_SET_BUS_SCALING_REQ	_IOWR(QSEECOM_IOC_MAGIC, 23, int)

/* The vendor handle: ion_sbuffer first (clients read it), then our state */
struct qseecom_handle {
	unsigned char *ion_sbuffer;
	int dev_fd;
	int buf_fd;
	uint32_t sb_len;
	uint32_t app_id;
	uint32_t listener_id;
	char app_name[64];
};

static int heap_alloc(uint32_t len, int *fd, void **map)
{
	int hfd = open(HEAP_DEV, O_RDWR | O_CLOEXEC);
	struct dma_heap_allocation_data d = { .len = len, .fd_flags = O_RDWR | O_CLOEXEC };

	if (hfd < 0) {
		LOG("%s: %s", HEAP_DEV, strerror(errno));
		return -errno;
	}
	if (ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &d) < 0) {
		int e = errno;
		LOG("heap alloc %u: %s", len, strerror(e));
		close(hfd);
		return -e;
	}
	close(hfd);
	*map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, d.fd, 0);
	if (*map == MAP_FAILED) {
		int e = errno;
		close(d.fd);
		return -e;
	}
	memset(*map, 0, len);
	*fd = d.fd;
	return 0;
}

static struct qseecom_handle *handle_new(uint32_t sb_size)
{
	struct qseecom_handle *h = calloc(1, sizeof(*h));
	struct qseecom_set_sb_mem_param_req mem;
	void *map;
	int ret;

	if (!h)
		return NULL;
	h->dev_fd = open(QSEECOM_DEV, O_RDWR | O_CLOEXEC);
	if (h->dev_fd < 0) {
		LOG("%s: %s", QSEECOM_DEV, strerror(errno));
		goto err;
	}
	/* page-rounded, like the vendor library's ION allocation */
	h->sb_len = (sb_size + 0xfff) & ~0xfffu;
	ret = heap_alloc(h->sb_len, &h->buf_fd, &map);
	if (ret)
		goto err;
	h->ion_sbuffer = map;
	mem.ifd_data_fd = h->buf_fd;
	mem.virt_sb_base = map;
	mem.sb_len = h->sb_len;
	if (ioctl(h->dev_fd, QSEECOM_IOCTL_SET_MEM_PARAM_REQ, &mem) < 0) {
		LOG("SET_MEM_PARAM: %s", strerror(errno));
		goto err;
	}
	return h;
err:
	if (h->ion_sbuffer)
		munmap(h->ion_sbuffer, h->sb_len);
	if (h->buf_fd > 0)
		close(h->buf_fd);
	if (h->dev_fd >= 0)
		close(h->dev_fd);
	free(h);
	return NULL;
}

static void handle_free(struct qseecom_handle *h)
{
	if (!h)
		return;
	if (h->ion_sbuffer)
		munmap(h->ion_sbuffer, h->sb_len);
	if (h->buf_fd > 0)
		close(h->buf_fd);
	if (h->dev_fd >= 0)
		close(h->dev_fd);
	free(h);
}

int QSEECom_start_app(struct QSEECom_handle **handle, const char *path,
		      const char *fname, uint32_t sb_size)
{
	struct qseecom_handle *h;
	struct qseecom_load_img_req load = {};
	struct qseecom_qseos_version_req ver;

	if (!handle || !fname)
		return -EINVAL;
	h = handle_new(sb_size);
	if (!h)
		return -1;
	if (ioctl(h->dev_fd, QSEECOM_IOCTL_GET_QSEOS_VERSION_REQ, &ver) == 0)
		LOG("qseos version %#x", ver.qseos_version);
	/* the kernel loads the image by name from its firmware directory; the
	 * client's path (/firmware/image on stock) is informational */
	(void)path;
	strncpy(load.img_name, fname, sizeof(load.img_name) - 1);
	load.ifd_data_fd = h->buf_fd;
	if (ioctl(h->dev_fd, QSEECOM_IOCTL_LOAD_APP_REQ, &load) < 0) {
		LOG("LOAD_APP %s: %s", fname, strerror(errno));
		handle_free(h);
		return -1;
	}
	h->app_id = load.app_id;
	strncpy(h->app_name, fname, sizeof(h->app_name) - 1);
	LOG("%s: app id %u, sb %u", fname, h->app_id, h->sb_len);
	*handle = (struct QSEECom_handle *)h;
	return 0;
}

int QSEECom_start_app_V2(struct QSEECom_handle **handle, const char *fname,
			 unsigned char *trustlet, uint32_t tlen, uint32_t sb_size)
{
	(void)trustlet; (void)tlen;
	return QSEECom_start_app(handle, NULL, fname, sb_size);
}

int QSEECom_shutdown_app(struct QSEECom_handle **handle)
{
	struct qseecom_handle *h;

	if (!handle || !*handle)
		return -EINVAL;
	h = (struct qseecom_handle *)*handle;
	ioctl(h->dev_fd, QSEECOM_IOCTL_UNLOAD_APP_REQ);
	handle_free(h);
	*handle = NULL;
	return 0;
}

int QSEECom_send_cmd(struct QSEECom_handle *handle, void *send_buf,
		     uint32_t sbuf_len, void *rcv_buf, uint32_t rbuf_len)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;
	struct qseecom_send_cmd_req req = {
		.cmd_req_buf = send_buf, .cmd_req_len = sbuf_len,
		.resp_buf = rcv_buf, .resp_len = rbuf_len,
	};

	if (!h || !send_buf || !rcv_buf)
		return -EINVAL;
	if (ioctl(h->dev_fd, QSEECOM_IOCTL_SEND_CMD_REQ, &req) < 0) {
		LOG("SEND_CMD: %s", strerror(errno));
		return -1;
	}
	return 0;
}

int QSEECom_send_modified_cmd(struct QSEECom_handle *handle, void *send_buf,
			      uint32_t sbuf_len, void *resp_buf, uint32_t rbuf_len,
			      struct QSEECom_ion_fd_info *ifd_data)
{
	/* the fd-carrying variant is for buffers outside the shared buffer
	 * (ION fds); not supported on mainline, plain send when no fds given */
	if (ifd_data) {
		int i;

		for (i = 0; i < 4; i++)
			if (ifd_data->data[i].fd > 0) {
				LOG("send_modified_cmd with fds: unsupported");
				errno = ENOTSUP;
				return -1;
			}
	}
	return QSEECom_send_cmd(handle, send_buf, sbuf_len, resp_buf, rbuf_len);
}

int QSEECom_send_modified_cmd_64(struct QSEECom_handle *handle, void *send_buf,
				 uint32_t sbuf_len, void *resp_buf,
				 uint32_t rbuf_len,
				 struct QSEECom_ion_fd_info *ifd_data)
{
	return QSEECom_send_modified_cmd(handle, send_buf, sbuf_len, resp_buf,
					 rbuf_len, ifd_data);
}

int QSEECom_register_listener(struct QSEECom_handle **handle, uint32_t lstnr_id,
			      uint32_t sb_length, uint32_t flags)
{
	struct qseecom_handle *h;
	struct qseecom_register_listener_req req;

	(void)flags;
	if (!handle)
		return -EINVAL;
	h = calloc(1, sizeof(*h));
	if (!h)
		return -ENOMEM;
	h->dev_fd = open(QSEECOM_DEV, O_RDWR | O_CLOEXEC);
	if (h->dev_fd < 0) {
		free(h);
		return -1;
	}
	h->sb_len = (sb_length + 0xfff) & ~0xfffu;
	if (heap_alloc(h->sb_len, &h->buf_fd, (void **)&h->ion_sbuffer)) {
		close(h->dev_fd);
		free(h);
		return -1;
	}
	req.listener_id = lstnr_id;
	req.ifd_data_fd = h->buf_fd;
	req.virt_sb_base = h->ion_sbuffer;
	req.sb_size = h->sb_len;
	if (ioctl(h->dev_fd, QSEECOM_IOCTL_REGISTER_LISTENER_REQ, &req) < 0) {
		LOG("REGISTER_LISTENER %#x: %s", lstnr_id, strerror(errno));
		handle_free(h);
		return -1;
	}
	h->listener_id = lstnr_id;
	LOG("listener %#x registered, sb %u", lstnr_id, h->sb_len);
	*handle = (struct QSEECom_handle *)h;
	return 0;
}

int QSEECom_unregister_listener(struct QSEECom_handle *handle)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;

	if (!h)
		return -EINVAL;
	ioctl(h->dev_fd, QSEECOM_IOCTL_UNREGISTER_LISTENER_REQ);
	handle_free(h);
	return 0;
}

int QSEECom_receive_req(struct QSEECom_handle *handle, void *buf, uint32_t len)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;

	if (!h)
		return -EINVAL;
	if (ioctl(h->dev_fd, QSEECOM_IOCTL_RECEIVE_REQ) < 0)
		return -1;
	/* the request is in the shared buffer; copy out if asked elsewhere */
	if (buf && buf != h->ion_sbuffer)
		memcpy(buf, h->ion_sbuffer, len < h->sb_len ? len : h->sb_len);
	return 0;
}

int QSEECom_send_resp(struct QSEECom_handle *handle, void *send_buf, uint32_t len)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;

	if (!h)
		return -EINVAL;
	if (send_buf && send_buf != h->ion_sbuffer)
		memcpy(h->ion_sbuffer, send_buf, len < h->sb_len ? len : h->sb_len);
	return ioctl(h->dev_fd, QSEECOM_IOCTL_SEND_RESP_REQ) < 0 ? -1 : 0;
}

int QSEECom_send_modified_resp(struct QSEECom_handle *handle, void *send_buf,
			       uint32_t len, struct QSEECom_ion_fd_info *ifd_data)
{
	(void)ifd_data;
	return QSEECom_send_resp(handle, send_buf, len);
}

int QSEECom_send_modified_resp_64(struct QSEECom_handle *handle, void *send_buf,
				  uint32_t len, struct QSEECom_ion_fd_info *ifd_data)
{
	return QSEECom_send_modified_resp(handle, send_buf, len, ifd_data);
}

int QSEECom_set_bandwidth(struct QSEECom_handle *handle, bool high)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;

	if (!h)
		return -EINVAL;
	return ioctl(h->dev_fd, high ? QSEECOM_IOCTL_PERF_ENABLE_REQ
				     : QSEECOM_IOCTL_PERF_DISABLE_REQ) < 0 ? -1 : 0;
}

int QSEECom_scale_bus_bandwidth(struct QSEECom_handle *handle, int mode)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;

	if (!h)
		return -EINVAL;
	return ioctl(h->dev_fd, QSEECOM_IOCTL_SET_BUS_SCALING_REQ, &mode) < 0 ? -1 : 0;
}

int QSEECom_app_load_query(struct QSEECom_handle *handle, char *app_name)
{
	struct qseecom_handle *h = (struct qseecom_handle *)handle;
	struct qseecom_qseos_app_load_query q = {};
	int fd, ret;

	if (!app_name)
		return -EINVAL;
	fd = h ? h->dev_fd : open(QSEECOM_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return -1;
	strncpy(q.app_name, app_name, sizeof(q.app_name) - 1);
	ret = ioctl(fd, QSEECOM_IOCTL_APP_LOADED_QUERY_REQ, &q);
	ret = (ret < 0 && errno == EEXIST) ? 0 : -1;	/* 0 = loaded, as vendor */
	if (!h)
		close(fd);
	return ret;
}

int QSEECom_get_app_info(struct QSEECom_handle *handle, void *info)
{
	(void)handle; (void)info;
	errno = ENOTSUP;
	return -1;
}

/* Not needed on this device: external ELFs, service commands, FDE keys */
int QSEECom_load_external_elf(struct QSEECom_handle **handle, const char *path,
			      const char *fname)
{ (void)handle; (void)path; (void)fname; errno = ENOTSUP; return -1; }
int QSEECom_unload_external_elf(struct QSEECom_handle **handle)
{ (void)handle; errno = ENOTSUP; return -1; }
int QSEECom_send_service_cmd(void *send_buf, uint32_t sbuf_len, void *rcv_buf,
			     uint32_t rbuf_len, int cmd_id)
{ (void)send_buf; (void)sbuf_len; (void)rcv_buf; (void)rbuf_len; (void)cmd_id; errno = ENOTSUP; return -1; }
int QSEECom_create_key(int usage, void *hash32)
{ (void)usage; (void)hash32; errno = ENOTSUP; return -1; }
int QSEECom_wipe_key(int usage) { (void)usage; errno = ENOTSUP; return -1; }
int QSEECom_update_key_user_info(int usage, void *cur, void *new)
{ (void)usage; (void)cur; (void)new; errno = ENOTSUP; return -1; }
int QSEECom_clear_key(int usage) { (void)usage; errno = ENOTSUP; return -1; }
