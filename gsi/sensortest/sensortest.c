// Lists the sensors the framework has, then streams each of the default
// accelerometer, gyroscope, magnetometer and light sensor (or only the
// types named) for a few seconds and reports the event rate, the first and
// last values and how old the events' timestamps are when they arrive.
// usage: sensortest [seconds] [rate Hz] [accel|gyro|mag|light ...]
#include <android/looper.h>
#include <android/sensor.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t boottime_ns(void)
{
	struct timespec t;
	clock_gettime(CLOCK_BOOTTIME, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static const struct { const char *name; int type; } kTypes[] = {
	{ "accel", ASENSOR_TYPE_ACCELEROMETER },
	{ "gyro", ASENSOR_TYPE_GYROSCOPE },
	{ "mag", ASENSOR_TYPE_MAGNETIC_FIELD },
	{ "light", ASENSOR_TYPE_LIGHT },
};

static void stream(ASensorManager *sm, int type, double seconds, int hz)
{
	const ASensor *s = ASensorManager_getDefaultSensor(sm, type);
	if (!s) {
		printf("type %d: no default sensor\n", type);
		return;
	}
	ALooper *looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
	ASensorEventQueue *q = ASensorManager_createEventQueue(sm, looper, 1, NULL, NULL);
	if (ASensorEventQueue_registerSensor(q, s, 1000000 / hz, 0) < 0) {
		printf("%s: can't register\n", ASensor_getName(s));
		ASensorManager_destroyEventQueue(sm, q);
		return;
	}
	int n = 0;
	int64_t t0 = 0, t1 = 0, lag_sum = 0, lag_max = 0;
	ASensorEvent first, last;
	const int64_t end = boottime_ns() + (int64_t)(seconds * 1e9);
	while (boottime_ns() < end) {
		if (ALooper_pollOnce(200, NULL, NULL, NULL) != 1)
			continue;
		ASensorEvent ev[16];
		ssize_t k;
		while ((k = ASensorEventQueue_getEvents(q, ev, 16)) > 0) {
			const int64_t now = boottime_ns();
			for (ssize_t i = 0; i < k; i++) {
				if (ev[i].type != type)
					continue;
				if (!n) { first = ev[i]; t0 = ev[i].timestamp; }
				last = ev[i];
				t1 = ev[i].timestamp;
				const int64_t lag = now - ev[i].timestamp;
				lag_sum += lag;
				if (lag > lag_max) lag_max = lag;
				n++;
			}
		}
	}
	ASensorEventQueue_disableSensor(q, s);
	ASensorManager_destroyEventQueue(sm, q);
	printf("%s: %d events", ASensor_getName(s), n);
	if (n > 1)
		printf(", %.1f Hz", (n - 1) / ((t1 - t0) / 1e9));
	if (n)
		printf(", lag avg %.1f ms max %.1f ms", lag_sum / n / 1e6, lag_max / 1e6);
	printf("\n");
	if (n) {
		printf("  first: %9.4f %9.4f %9.4f\n", first.data[0], first.data[1], first.data[2]);
		printf("  last:  %9.4f %9.4f %9.4f\n", last.data[0], last.data[1], last.data[2]);
	}
}

int main(int argc, char **argv)
{
	const double seconds = argc > 1 ? atof(argv[1]) : 3;
	const int hz = argc > 2 ? atoi(argv[2]) : 50;
	ASensorManager *sm = ASensorManager_getInstanceForPackage("sensortest");
	ASensorList list;
	const int count = ASensorManager_getSensorList(sm, &list);
	for (int i = 0; i < count; i++) {
		const ASensor *s = list[i];
		printf("%2d %-28s %-20s type %2d res %g delay %d us\n", i, ASensor_getName(s),
		       ASensor_getVendor(s), ASensor_getType(s), ASensor_getResolution(s),
		       ASensor_getMinDelay(s));
	}
	for (size_t t = 0; t < sizeof(kTypes) / sizeof(kTypes[0]); t++) {
		int wanted = argc <= 3;
		for (int a = 3; a < argc; a++)
			if (!strcmp(argv[a], kTypes[t].name)) wanted = 1;
		if (wanted)
			stream(sm, kTypes[t].type, seconds, hz);
	}
	return 0;
}
