/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * QSEEComAPI: the Qualcomm Secure Execution Environment client API, as the
 * vendor HALs and qseecomd use it. This is an open implementation for
 * mainline Linux: /dev/qseecom with the downstream ioctl ABI, shared
 * buffers from a DMA-BUF heap (the vendor library used ION).
 */
#ifndef QSEECOMAPI_H
#define QSEECOMAPI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSEECOM_ALIGN_SIZE	0x40
#define QSEECOM_ALIGN_MASK	(QSEECOM_ALIGN_SIZE - 1)
#define QSEECOM_ALIGN(x)	(((x) + QSEECOM_ALIGN_MASK) & ~QSEECOM_ALIGN_MASK)

/*
 * The handle a client gets from QSEECom_start_app(). ion_sbuffer is the
 * first field, as clients read it directly for their request/response
 * buffers (it points at the shared buffer's mapping).
 */
struct QSEECom_handle {
	unsigned char *ion_sbuffer;
};

struct QSEECom_ion_fd_data {
	int32_t fd;
	uint32_t cmd_buf_offset;
};

struct QSEECom_ion_fd_info {
	struct QSEECom_ion_fd_data data[4];
};

int QSEECom_start_app(struct QSEECom_handle **handle, const char *path,
		      const char *fname, uint32_t sb_size);
int QSEECom_start_app_V2(struct QSEECom_handle **handle, const char *fname,
			 unsigned char *trustlet, uint32_t tlen,
			 uint32_t sb_size);
int QSEECom_shutdown_app(struct QSEECom_handle **handle);
int QSEECom_load_external_elf(struct QSEECom_handle **handle,
			      const char *path, const char *fname);
int QSEECom_unload_external_elf(struct QSEECom_handle **handle);
int QSEECom_register_listener(struct QSEECom_handle **handle,
			      uint32_t lstnr_id, uint32_t sb_length,
			      uint32_t flags);
int QSEECom_unregister_listener(struct QSEECom_handle *handle);
int QSEECom_send_cmd(struct QSEECom_handle *handle, void *send_buf,
		     uint32_t sbuf_len, void *rcv_buf, uint32_t rbuf_len);
int QSEECom_send_modified_cmd(struct QSEECom_handle *handle, void *send_buf,
			      uint32_t sbuf_len, void *resp_buf,
			      uint32_t rbuf_len,
			      struct QSEECom_ion_fd_info *ifd_data);
int QSEECom_send_modified_cmd_64(struct QSEECom_handle *handle,
				 void *send_buf, uint32_t sbuf_len,
				 void *resp_buf, uint32_t rbuf_len,
				 struct QSEECom_ion_fd_info *ifd_data);
int QSEECom_receive_req(struct QSEECom_handle *handle, void *buf,
			uint32_t len);
int QSEECom_send_resp(struct QSEECom_handle *handle, void *send_buf,
		      uint32_t len);
int QSEECom_send_modified_resp(struct QSEECom_handle *handle, void *send_buf,
			       uint32_t len,
			       struct QSEECom_ion_fd_info *ifd_data);
int QSEECom_send_modified_resp_64(struct QSEECom_handle *handle,
				  void *send_buf, uint32_t len,
				  struct QSEECom_ion_fd_info *ifd_data);
int QSEECom_set_bandwidth(struct QSEECom_handle *handle, bool high);
int QSEECom_scale_bus_bandwidth(struct QSEECom_handle *handle, int mode);
int QSEECom_app_load_query(struct QSEECom_handle *handle, char *app_name);
int QSEECom_get_app_info(struct QSEECom_handle *handle, void *info);
int QSEECom_send_service_cmd(void *send_buf, uint32_t sbuf_len,
			     void *rcv_buf, uint32_t rbuf_len, int cmd_id);
int QSEECom_create_key(int usage, void *hash32);
int QSEECom_wipe_key(int usage);
int QSEECom_update_key_user_info(int usage, void *current_hash32,
				 void *new_hash32);
int QSEECom_clear_key(int usage);

#ifdef __cplusplus
}
#endif
#endif
