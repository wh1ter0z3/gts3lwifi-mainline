// SPDX-License-Identifier: MIT
/*
 * gsi-dhcpd: the smallest DHCP server, for gsi-init's USB network link: one
 * client, one address. It answers DISCOVER with an OFFER, REQUEST with an ACK
 * (a NAK for any other address) and INFORM with an ACK, and gives no router
 * and no DNS server, so the PC reaches the tablet (root shell, adb) without
 * sending its other traffic there (pmOS's unudhcpd does the same for pmOS).
 * A static program in the boot ramdisk, started by gsi-init with the link.
 * usage: gsi-dhcpd <interface> <server address> <client address>
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BOOTREQUEST	1
#define BOOTREPLY	2
#define DHCP_MAGIC	0x63825363
#define LEASE_SECONDS	864000

enum { DISCOVER = 1, OFFER, REQUEST, DECLINE, ACK, NAK, RELEASE, INFORM };

struct bootp {
	uint8_t op, htype, hlen, hops;
	uint32_t xid;
	uint16_t secs, flags;
	uint32_t ciaddr, yiaddr, siaddr, giaddr;
	uint8_t chaddr[16];
	uint8_t sname[64], file[128];
	uint32_t magic;
	uint8_t options[312];
} __attribute__((packed));

/* option `code` of a received message (n bytes), or NULL; its length in *len */
static const uint8_t *option(const struct bootp *p, size_t n, uint8_t code, uint8_t *len)
{
	const uint8_t *o = p->options, *end = (const uint8_t *)p + n;

	while (o < end && *o != 255) {
		if (*o == 0) {
			o++;
			continue;
		}
		if (o + 2 > end || o + 2 + o[1] > end)
			break;
		if (*o == code) {
			*len = o[1];
			return o + 2;
		}
		o += 2 + o[1];
	}
	return NULL;
}

static uint8_t *put(uint8_t *o, uint8_t code, const void *v, uint8_t len)
{
	o[0] = code;
	o[1] = len;
	memcpy(o + 2, v, len);
	return o + 2 + len;
}

int main(int argc, char **argv)
{
	struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(67) };
	struct in_addr server, client;
	const uint32_t mask = htonl(0xffff0000), lease = htonl(LEASE_SECONDS);
	int s, one = 1;

	if (argc != 4 || !inet_aton(argv[2], &server) || !inet_aton(argv[3], &client)) {
		fprintf(stderr, "usage: %s <interface> <server address> <client address>\n", argv[0]);
		return 1;
	}
	s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0 || setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) ||
	    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) ||
	    setsockopt(s, SOL_SOCKET, SO_BINDTODEVICE, argv[1], strlen(argv[1]) + 1) ||
	    bind(s, (struct sockaddr *)&sa, sizeof(sa))) {
		perror("gsi-dhcpd");
		return 1;
	}
	for (;;) {
		struct bootp p, r;
		struct sockaddr_in to = { .sin_family = AF_INET, .sin_port = htons(68),
					  .sin_addr.s_addr = htonl(INADDR_BROADCAST) };
		ssize_t n = recv(s, &p, sizeof(p), 0);
		const uint8_t *t, *req;
		uint8_t tl, rl, type, *o;

		if (n < (ssize_t)offsetof(struct bootp, options) || p.op != BOOTREQUEST ||
		    p.htype != 1 || p.hlen != 6 || ntohl(p.magic) != DHCP_MAGIC)
			continue;
		t = option(&p, n, 53, &tl);
		if (!t || tl != 1)
			continue;
		switch (*t) {
		case DISCOVER:
			type = OFFER;
			break;
		case REQUEST:	/* selecting: requested address; renewing: ciaddr */
			req = option(&p, n, 50, &rl);
			if (req && rl == 4)
				type = memcmp(req, &client.s_addr, 4) ? NAK : ACK;
			else
				type = p.ciaddr == client.s_addr ? ACK : NAK;
			break;
		case INFORM:
			type = ACK;
			break;
		default:
			continue;
		}
		memset(&r, 0, sizeof(r));
		r.op = BOOTREPLY;
		r.htype = 1;
		r.hlen = 6;
		r.xid = p.xid;
		r.flags = p.flags;
		r.giaddr = p.giaddr;
		memcpy(r.chaddr, p.chaddr, sizeof(r.chaddr));
		r.magic = htonl(DHCP_MAGIC);
		if (*t == INFORM) {
			r.ciaddr = p.ciaddr;
			if (p.ciaddr)
				to.sin_addr.s_addr = p.ciaddr;
		} else if (type != NAK) {
			r.yiaddr = client.s_addr;
		}
		o = put(r.options, 53, &type, 1);
		o = put(o, 54, &server.s_addr, 4);
		if (type != NAK) {
			o = put(o, 1, &mask, 4);
			if (*t != INFORM)
				o = put(o, 51, &lease, 4);
		}
		*o++ = 255;
		if (sendto(s, &r, o - (uint8_t *)&r, 0, (struct sockaddr *)&to, sizeof(to)) < 0)
			perror("gsi-dhcpd: send");
		else
			fprintf(stderr, "gsi-dhcpd: %s %s for %02x:%02x:%02x:%02x:%02x:%02x\n",
				type == OFFER ? "offer" : type == ACK ? "ack" : "nak",
				type == NAK ? "-" : inet_ntoa(client),
				p.chaddr[0], p.chaddr[1], p.chaddr[2], p.chaddr[3], p.chaddr[4], p.chaddr[5]);
	}
}
