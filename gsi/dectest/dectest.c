// Decodes a video file's first video track with a named MediaCodec decoder to
// ByteBuffers and reports frames, time and the first frame's checksum.
// usage: dectest <file> <codec name>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 3) { fprintf(stderr, "usage: %s <file> <codec>\n", argv[0]); return 1; }
	int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
	AMediaExtractor *ex = AMediaExtractor_new();
	if (AMediaExtractor_setDataSourceFd(ex, fd, 0, st.st_size)) { printf("extractor failed\n"); return 1; }
	int track = -1; AMediaFormat *fmt = NULL; const char *mime = NULL;
	for (size_t i = 0; i < AMediaExtractor_getTrackCount(ex); i++) {
		fmt = AMediaExtractor_getTrackFormat(ex, i);
		if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && !strncmp(mime, "video/", 6)) { track = i; break; }
	}
	if (track < 0) { printf("no video track\n"); return 1; }
	AMediaExtractor_selectTrack(ex, track);
	AMediaCodec *c = AMediaCodec_createCodecByName(argv[2]);
	if (!c) { printf("no codec %s\n", argv[2]); return 1; }
	if (AMediaCodec_configure(c, fmt, NULL, NULL, 0) || AMediaCodec_start(c)) { printf("configure/start failed\n"); return 1; }
	int frames = 0, in_eos = 0; unsigned long sum = 0; double t0 = now();
	while (1) {
		if (!in_eos) {
			ssize_t ib = AMediaCodec_dequeueInputBuffer(c, 10000);
			if (ib >= 0) {
				size_t cap; uint8_t *buf = AMediaCodec_getInputBuffer(c, ib, &cap);
				ssize_t n = AMediaExtractor_readSampleData(ex, buf, cap);
				if (n < 0) { AMediaCodec_queueInputBuffer(c, ib, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM); in_eos = 1; }
				else { AMediaCodec_queueInputBuffer(c, ib, 0, n, AMediaExtractor_getSampleTime(ex), 0); AMediaExtractor_advance(ex); }
			}
		}
		AMediaCodecBufferInfo info;
		ssize_t ob = AMediaCodec_dequeueOutputBuffer(c, &info, 10000);
		if (ob >= 0) {
			if (info.size > 0) {
				if (frames == 0) {
					size_t sz; uint8_t *o = AMediaCodec_getOutputBuffer(c, ob, &sz);
					for (int i = 0; o && i < info.size; i++) sum += o[info.offset + i];
				}
				frames++;
			}
			AMediaCodec_releaseOutputBuffer(c, ob, false);
			if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) break;
		} else if (ob == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			AMediaFormat *of = AMediaCodec_getOutputFormat(c);
			printf("output format: %s\n", AMediaFormat_toString(of));
			AMediaFormat_delete(of);
		}
		if (now() - t0 > 120) { printf("timeout\n"); break; }
	}
	double t = now() - t0;
	printf("%s: %d frames in %.2f s = %.1f fps, first frame sum %lu\n", argv[2], frames, t, frames / t, sum);
	AMediaCodec_stop(c); AMediaCodec_delete(c);
	return 0;
}
