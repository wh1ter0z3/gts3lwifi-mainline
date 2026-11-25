// Copyright (C) 2026 The gts3lwifi-mainline project
// SPDX-License-Identifier: Apache-2.0
//
// faketap: a uinput touchscreen shaped like the Tab S3's RMI4 one after the
// device tree's swap (panel frame, 2048 x 1536), for testing touch without
// a panel. usage: faketap <x> <y> | shot | swipe <x1> <y1> <x2> <y2> ...
// (panel coordinates; "shot" holds Power + Volume Down: the recovery's
// screenshot)
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int fd;

static void emit(int type, int code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) != sizeof(ev))
		perror("write");
}

static void axis(int code, int max)
{
	struct uinput_abs_setup a;
	memset(&a, 0, sizeof(a));
	a.code = code;
	a.absinfo.maximum = max;
	ioctl(fd, UI_ABS_SETUP, &a);
	ioctl(fd, UI_SET_ABSBIT, code);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <x> <y> | shot ...\n", argv[0]);
		return 1;
	}
	fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("/dev/uinput");
		return 1;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH);
	ioctl(fd, UI_SET_KEYBIT, KEY_POWER);
	ioctl(fd, UI_SET_KEYBIT, KEY_VOLUMEDOWN);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	ioctl(fd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
	axis(ABS_X, 2047);
	axis(ABS_Y, 1535);
	axis(ABS_MT_SLOT, 9);
	axis(ABS_MT_TRACKING_ID, 65535);
	axis(ABS_MT_POSITION_X, 2047);
	axis(ABS_MT_POSITION_Y, 1535);

	struct uinput_setup us;
	memset(&us, 0, sizeof(us));
	us.id.bustype = BUS_VIRTUAL;
	strcpy(us.name, "faketap RMI4-like touchscreen");
	ioctl(fd, UI_DEV_SETUP, &us);
	if (ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("UI_DEV_CREATE");
		return 1;
	}
	sleep(4);	// the recovery reloads /dev/input every 2 s

	for (int i = 1; i < argc; i += 2) {
		if (!strcmp(argv[i], "shot")) {
			emit(EV_KEY, KEY_VOLUMEDOWN, 1);
			emit(EV_KEY, KEY_POWER, 1);
			emit(EV_SYN, SYN_REPORT, 0);
			usleep(700000);
			emit(EV_KEY, KEY_POWER, 0);
			emit(EV_KEY, KEY_VOLUMEDOWN, 0);
			emit(EV_SYN, SYN_REPORT, 0);
			printf("shot\n");
			sleep(2);
			i--;
			continue;
		}
		if (!strcmp(argv[i], "swipe") && i + 4 < argc) {
			int x1 = atoi(argv[i + 1]), y1 = atoi(argv[i + 2]);
			int x2 = atoi(argv[i + 3]), y2 = atoi(argv[i + 4]);
			emit(EV_ABS, ABS_MT_SLOT, 0);
			emit(EV_ABS, ABS_MT_TRACKING_ID, 100 + i);
			for (int s = 0; s <= 20; s++) {
				int x = x1 + (x2 - x1) * s / 20, y = y1 + (y2 - y1) * s / 20;
				emit(EV_ABS, ABS_MT_POSITION_X, x);
				emit(EV_ABS, ABS_MT_POSITION_Y, y);
				if (s == 0)
					emit(EV_KEY, BTN_TOUCH, 1);
				emit(EV_ABS, ABS_X, x);
				emit(EV_ABS, ABS_Y, y);
				emit(EV_SYN, SYN_REPORT, 0);
				usleep(15000);
			}
			emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
			emit(EV_KEY, BTN_TOUCH, 0);
			emit(EV_SYN, SYN_REPORT, 0);
			printf("swipe %d,%d -> %d,%d\n", x1, y1, x2, y2);
			sleep(2);
			i += 3;
			continue;
		}
		if (i + 1 >= argc)
			break;
		int x = atoi(argv[i]), y = atoi(argv[i + 1]);
		emit(EV_ABS, ABS_MT_SLOT, 0);
		emit(EV_ABS, ABS_MT_TRACKING_ID, i);
		emit(EV_ABS, ABS_MT_POSITION_X, x);
		emit(EV_ABS, ABS_MT_POSITION_Y, y);
		emit(EV_KEY, BTN_TOUCH, 1);
		emit(EV_ABS, ABS_X, x);
		emit(EV_ABS, ABS_Y, y);
		emit(EV_SYN, SYN_REPORT, 0);
		usleep(120000);
		emit(EV_ABS, ABS_MT_TRACKING_ID, -1);
		emit(EV_KEY, BTN_TOUCH, 0);
		emit(EV_SYN, SYN_REPORT, 0);
		printf("tap %d,%d\n", x, y);
		sleep(2);
	}
	sleep(1);
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}
