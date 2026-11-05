// SPDX-License-Identifier: BSD-3-Clause
/*
 * qseecom-km: talk to Samsung's keymaster through the open libQSEEComAPI,
 * the way the stock libkeymaster_helper does (KM_TZOpenConnection +
 * KM_TZRunCommand): start_app("skeymast", 0x14080 + 0x40 shared buffer), request
 * {cmd, 0xa000, 0xa000} at the start of the buffer, the 64-byte response after it
 * (the layout of the T820XXU3CTD5 vendor HAL, which LineageOS ships unchanged).
 *
 *   qseecom-km [cmd ...]    default: 1 (get_supported_algorithms), 0x12 (SelfTest)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "QSEEComAPI.h"

#define SB	0x14080
#define RSP	0x40

int main(int argc, char **argv)
{
	struct QSEECom_handle *h = NULL;
	uint32_t *req, *rsp;
	int i, ret;

	ret = QSEECom_start_app(&h, "/firmware/image", "skeymast", SB + RSP);
	if (ret) {
		fprintf(stderr, "start_app: %d\n", ret);
		return 1;
	}
	req = (uint32_t *)h->ion_sbuffer;
	rsp = (uint32_t *)(h->ion_sbuffer + SB);
	printf("skeymast started, buffer %p\n", h->ion_sbuffer);
	for (i = 1; i < (argc > 1 ? argc : 3); i++) {
		uint32_t cmd = argc > 1 ? strtoul(argv[i], NULL, 0) : (i == 1 ? 1 : 0x12);

		memset(req, 0, SB + RSP);
		req[0] = cmd; req[1] = 0xa000; req[2] = 0xa000;
		rsp[0] = (uint32_t)-1000;
		QSEECom_set_bandwidth(h, 1);
		ret = QSEECom_send_cmd(h, req, SB, rsp, RSP);
		QSEECom_set_bandwidth(h, 0);
		printf("cmd %#x: send %d, rsp[0..5] %08x %08x %08x %08x %08x %08x\n", cmd, ret,
		       rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5]);
	}
	QSEECom_shutdown_app(&h);
	return 0;
}
