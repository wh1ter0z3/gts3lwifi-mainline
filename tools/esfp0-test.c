// SPDX-License-Identifier: GPL-2.0
/*
 * esfp0-test: exercise the Egis ET5xx control driver through Samsung's
 * /dev/esfp0 ABI (EGIS_IOC_MESSAGE with control opcodes), as the vendor
 * biometric library does: power on, SPI clock on, reset, wait for DRDY
 * (optional), clock off, power off.
 *
 *   esfp0-test            power/clock/reset sequence
 *   esfp0-test wait <s>   also arm DRDY and poll for a finger for <s> seconds
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct egis_ioc_transfer {
	uint64_t tx_buf, rx_buf;
	uint32_t len, speed_hz;
	uint16_t delay_usecs;
	uint8_t bits_per_word, cs_change, opcode, pad[3];
};
#define EGIS_IOC_MESSAGE _IOW('k', 0, char[sizeof(struct egis_ioc_transfer)])
#define FP_SENSOR_RESET		0x04
#define FP_POWER_CONTROL_ET5XX	0x18
#define FP_SET_SPI_CLOCK	0x06
#define FP_DISABLE_SPI_CLOCK	0x10
#define FP_SET_SENSOR_TYPE	0x14
#define INT_TRIGGER_INIT	0xa4
#define INT_TRIGGER_CLOSE	0xa5

static int fd;

static int op(const char *name, uint8_t opcode, uint32_t len, uint32_t hz)
{
	struct egis_ioc_transfer t = { .opcode = opcode, .len = len, .speed_hz = hz };
	int ret = ioctl(fd, EGIS_IOC_MESSAGE, &t);

	printf("%-20s -> %d%s%s\n", name, ret, ret ? ": " : "", ret ? strerror(errno) : "");
	return ret;
}

static void show(const char *attr)
{
	char path[96], buf[64] = "";
	FILE *f;

	snprintf(path, sizeof(path), "/sys/class/fingerprint/fingerprint/%s", attr);
	f = fopen(path, "r");
	if (f) { if (fgets(buf, sizeof(buf), f)) buf[strcspn(buf, "\n")] = 0; fclose(f); }
	printf("  %-12s %s\n", attr, buf);
}

int main(int argc, char **argv)
{
	fd = open("/dev/esfp0", O_RDWR);
	if (fd < 0) { perror("/dev/esfp0"); return 1; }
	show("name"); show("vendor"); show("adm"); show("type_check");
	op("power on", FP_POWER_CONTROL_ET5XX, 1, 0);
	op("spi clock 12.5 MHz", FP_SET_SPI_CLOCK, 0, 12500000);
	show("bfs_values");
	op("sensor reset", FP_SENSOR_RESET, 0, 0);
	op("sensor type EGIS", FP_SET_SENSOR_TYPE, 3, 0);
	show("type_check");
	if (argc > 2 && !strcmp(argv[1], "wait")) {
		struct pollfd p = { .fd = fd, .events = POLLIN };
		int s = atoi(argv[2]), r;

		op("drdy irq init", INT_TRIGGER_INIT, 0, 0);
		printf("touch the Home key... (%d s)\n", s);
		r = poll(&p, 1, s * 1000);
		printf("poll -> %d%s\n", r, r > 0 ? " (DRDY!)" : r == 0 ? " (timeout)" : "");
		show("intcnt");
		op("drdy irq close", INT_TRIGGER_CLOSE, 0, 0);
	}
	op("spi clock off", FP_DISABLE_SPI_CLOCK, 0, 0);
	op("power off", FP_POWER_CONTROL_ET5XX, 0, 0);
	show("resetcnt");
	close(fd);
	return 0;
}
