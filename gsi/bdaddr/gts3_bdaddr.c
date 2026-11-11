// SPDX-License-Identifier: MIT
/*
 * gts3-bdaddr: gives the QCA6174 Bluetooth controller its per-device public
 * address, from Samsung's efs (bluetooth/bt_addr, "aa:bb:cc:dd:ee:ff"), with
 * the kernel's Bluetooth management interface (MGMT_OP_SET_PUBLIC_ADDRESS),
 * as BlueZ's `btmgmt public-addr` does. hci_qca leaves hci0 unconfigured
 * until it has one (its NVM file only carries a placeholder), and Android's
 * Bluetooth stack then waits forever. A static program, for gsi-init: the
 * standalone build has no BlueZ.
 * usage: gts3-bdaddr <bt_addr file>
 */
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH 31
#endif
#define BTPROTO_HCI 1
#define HCI_CHANNEL_CONTROL 3
#define HCI_DEV_NONE 0xffff
#define MGMT_OP_SET_PUBLIC_ADDRESS 0x0039
#define MGMT_EV_CMD_COMPLETE 0x0001
#define MGMT_EV_CMD_STATUS 0x0002

struct sockaddr_hci {
	sa_family_t hci_family;
	uint16_t hci_dev;
	uint16_t hci_channel;
};

struct mgmt_hdr {
	uint16_t opcode;
	uint16_t index;
	uint16_t len;
} __attribute__((packed));

/* the status of MGMT_OP_SET_PUBLIC_ADDRESS for hci0, or -1 */
static int set_public_address(const uint8_t addr[6])
{
	struct sockaddr_hci sa = { AF_BLUETOOTH, HCI_DEV_NONE, HCI_CHANNEL_CONTROL };
	struct {
		struct mgmt_hdr h;
		uint8_t bdaddr[6];
	} __attribute__((packed)) cmd = { { MGMT_OP_SET_PUBLIC_ADDRESS, 0, 6 }, { 0 } };
	uint8_t buf[512];
	int s, i, ret = -1;

	for (i = 0; i < 6; i++)
		cmd.bdaddr[i] = addr[5 - i];	/* little endian on the wire */
	s = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
	if (s < 0 || bind(s, (struct sockaddr *)&sa, sizeof(sa)) ||
	    write(s, &cmd, sizeof(cmd)) != (ssize_t)sizeof(cmd))
		goto out;
	for (;;) {
		struct pollfd p = { s, POLLIN, 0 };
		struct mgmt_hdr *h = (struct mgmt_hdr *)buf;
		ssize_t n;

		if (poll(&p, 1, 3000) <= 0)
			goto out;
		n = read(s, buf, sizeof(buf));
		if (n < (ssize_t)sizeof(*h) + 3)
			continue;
		if ((h->opcode == MGMT_EV_CMD_COMPLETE || h->opcode == MGMT_EV_CMD_STATUS) &&
		    (buf[6] | buf[7] << 8) == MGMT_OP_SET_PUBLIC_ADDRESS) {
			ret = buf[8];
			goto out;
		}
	}
out:
	if (s >= 0)
		close(s);
	return ret;
}

int main(int argc, char **argv)
{
	unsigned int a[6];
	uint8_t addr[6];
	char line[64] = "";
	FILE *f;
	int i, st = -1;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <bt_addr file>\n", argv[0]);
		return 2;
	}
	f = fopen(argv[1], "r");
	if (!f || !fgets(line, sizeof(line), f) ||
	    sscanf(line, "%2x:%2x:%2x:%2x:%2x:%2x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) != 6) {
		fprintf(stderr, "gts3-bdaddr: no address in %s\n", argv[1]);
		return 1;
	}
	fclose(f);
	for (i = 0; i < 6; i++)
		addr[i] = a[i];
	/* hci0 shows up before hci_qca has loaded the patch and NVM; the
	 * management interface takes the address only after that */
	for (i = 0; i < 30; i++) {
		st = set_public_address(addr);
		if (st == 0)
			return 0;
		sleep(1);
	}
	fprintf(stderr, "gts3-bdaddr: controller not ready (status %d)\n", st);
	return 1;
}
