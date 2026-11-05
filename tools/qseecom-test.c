// SPDX-License-Identifier: GPL-2.0
/*
 * qseecom-test: exercise /dev/qseecom with the downstream ABI, using a
 * DMA-BUF heap for the shared buffers (no ION on mainline).
 *
 *   qseecom-test app <name> <cmd>     load an app, send Samsung-keymaster
 *                                     style command <cmd>, print the status
 *   qseecom-test listener <id>        register listener <id> and answer its
 *                                     requests (status 0) until killed
 *
 * Build: cc -O2 -o qseecom-test qseecom-test.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/dma-heap.h>

#define QSEECOM_IOC_MAGIC 0x97
struct qseecom_register_listener_req { uint32_t listener_id; int32_t ifd_data_fd; void *virt_sb_base; uint32_t sb_size; };
struct qseecom_send_cmd_req { void *cmd_req_buf; unsigned int cmd_req_len; void *resp_buf; unsigned int resp_len; };
struct qseecom_load_img_req { uint32_t mdt_len; uint32_t img_len; int32_t ifd_data_fd; char img_name[64]; uint32_t app_arch; uint32_t app_id; };
struct qseecom_set_sb_mem_param_req { int32_t ifd_data_fd; void *virt_sb_base; uint32_t sb_len; };
struct qseecom_qseos_version_req { unsigned int qseos_version; };
struct qseecom_qseos_app_load_query { char app_name[64]; uint32_t app_id; uint32_t app_arch; };
#define QSEECOM_IOCTL_REGISTER_LISTENER_REQ _IOWR(QSEECOM_IOC_MAGIC, 1, struct qseecom_register_listener_req)
#define QSEECOM_IOCTL_UNREGISTER_LISTENER_REQ _IO(QSEECOM_IOC_MAGIC, 2)
#define QSEECOM_IOCTL_SEND_CMD_REQ _IOWR(QSEECOM_IOC_MAGIC, 3, struct qseecom_send_cmd_req)
#define QSEECOM_IOCTL_RECEIVE_REQ _IO(QSEECOM_IOC_MAGIC, 5)
#define QSEECOM_IOCTL_SEND_RESP_REQ _IO(QSEECOM_IOC_MAGIC, 6)
#define QSEECOM_IOCTL_LOAD_APP_REQ _IOWR(QSEECOM_IOC_MAGIC, 7, struct qseecom_load_img_req)
#define QSEECOM_IOCTL_SET_MEM_PARAM_REQ _IOWR(QSEECOM_IOC_MAGIC, 8, struct qseecom_set_sb_mem_param_req)
#define QSEECOM_IOCTL_GET_QSEOS_VERSION_REQ _IOWR(QSEECOM_IOC_MAGIC, 10, struct qseecom_qseos_version_req)
#define QSEECOM_IOCTL_APP_LOADED_QUERY_REQ _IOWR(QSEECOM_IOC_MAGIC, 15, struct qseecom_qseos_app_load_query)

static const char *heap = "/dev/dma_heap/qseecom@fec00000";

static int heap_alloc(size_t len, void **map)
{
	int hfd = open(heap, O_RDWR);
	struct dma_heap_allocation_data d = { .len = len, .fd_flags = O_RDWR | O_CLOEXEC };

	if (hfd < 0) { perror(heap); return -1; }
	if (ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &d) < 0) { perror("DMA_HEAP_IOCTL_ALLOC"); close(hfd); return -1; }
	close(hfd);
	*map = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, d.fd, 0);
	if (*map == MAP_FAILED) { perror("mmap"); close(d.fd); return -1; }
	return d.fd;
}

static int do_app(int fd, const char *name, unsigned cmd)
{
	size_t sb_len = 0x10000 + 0x10000;
	void *sb;
	int bfd = heap_alloc(sb_len, &sb);
	struct qseecom_set_sb_mem_param_req mem = { .ifd_data_fd = bfd, .virt_sb_base = sb, .sb_len = sb_len };
	struct qseecom_load_img_req load = { .ifd_data_fd = bfd };
	struct qseecom_qseos_version_req ver;
	struct qseecom_qseos_app_load_query q;
	uint32_t *req = sb, *rsp = (uint32_t *)((char *)sb + 0x10000);
	struct qseecom_send_cmd_req s = { .cmd_req_buf = req, .cmd_req_len = 0x10000, .resp_buf = rsp, .resp_len = 0x10000 };

	if (bfd < 0) return 1;
	if (ioctl(fd, QSEECOM_IOCTL_GET_QSEOS_VERSION_REQ, &ver) == 0) printf("qseos version %#x\n", ver.qseos_version);
	if (ioctl(fd, QSEECOM_IOCTL_SET_MEM_PARAM_REQ, &mem) < 0) { perror("SET_MEM_PARAM"); return 1; }
	strncpy(q.app_name, name, sizeof(q.app_name) - 1); q.app_name[63] = 0;
	errno = 0; ioctl(fd, QSEECOM_IOCTL_APP_LOADED_QUERY_REQ, &q);
	printf("query %s: %s%s\n", name, strerror(errno), errno == EEXIST ? " (loaded)" : "");
	strncpy(load.img_name, name, sizeof(load.img_name) - 1);
	if (ioctl(fd, QSEECOM_IOCTL_LOAD_APP_REQ, &load) < 0) { perror("LOAD_APP"); return 1; }
	printf("%s: app id %u\n", name, load.app_id);
	memset(sb, 0, sb_len);
	req[0] = cmd; req[1] = 0xa000; req[2] = 0xa000;
	rsp[0] = (uint32_t)-1000;
	if (ioctl(fd, QSEECOM_IOCTL_SEND_CMD_REQ, &s) < 0) { perror("SEND_CMD"); return 1; }
	printf("cmd %#x: status %d, rsp %08x %08x %08x %08x %08x %08x\n", cmd, (int)rsp[3],
	       rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5]);
	return 0;
}

static int do_listener(int fd, unsigned id)
{
	size_t sb_len = 0x20000;
	void *sb;
	int bfd = heap_alloc(sb_len, &sb);
	struct qseecom_register_listener_req r = { .listener_id = id, .ifd_data_fd = bfd, .virt_sb_base = sb, .sb_size = sb_len };
	uint32_t *w = sb;

	if (bfd < 0) return 1;
	if (ioctl(fd, QSEECOM_IOCTL_REGISTER_LISTENER_REQ, &r) < 0) { perror("REGISTER_LISTENER"); return 1; }
	printf("listener %#x registered, waiting\n", id);
	for (;;) {
		if (ioctl(fd, QSEECOM_IOCTL_RECEIVE_REQ) < 0) { perror("RECEIVE_REQ"); break; }
		printf("listener %#x: request %08x %08x %08x %08x\n", id, w[0], w[1], w[2], w[3]);
		fflush(stdout);
		/* a real service decodes the request and fills the response here */
		if (ioctl(fd, QSEECOM_IOCTL_SEND_RESP_REQ) < 0) { perror("SEND_RESP"); break; }
	}
	ioctl(fd, QSEECOM_IOCTL_UNREGISTER_LISTENER_REQ);
	return 0;
}

int main(int argc, char **argv)
{
	int fd;

	if (argc < 3) { fprintf(stderr, "usage: %s app <name> <cmd> | listener <id>\n", argv[0]); return 2; }
	fd = open("/dev/qseecom", O_RDWR);
	if (fd < 0) { perror("/dev/qseecom"); return 1; }
	if (!strcmp(argv[1], "app") && argc > 3)
		return do_app(fd, argv[2], strtoul(argv[3], NULL, 0));
	if (!strcmp(argv[1], "listener"))
		return do_listener(fd, strtoul(argv[2], NULL, 0));
	return 2;
}
