// Decodes a video file's first video track with a named MediaCodec decoder to
// ByteBuffers (or, with "surface", to an ImageReader's surface, as players
// do) and reports frames, time and checksums of the first frame: of the whole
// buffer, and of its luma in the track's size (comparable between decoders).
// With "seek", it flushes the codec after 30 frames and starts over. The codec
// name "auto" takes the first decoder for the track's type, as players do.
// usage: dectest <file> <codec name> [surface] [seek]
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

// a surface's producer (the codec service) calls back into this process:
// binder threads, from libbinder_ndk's platform API (not in the NDK headers)
static void binder_threads(void)
{
	void *l = dlopen("libbinder_ndk.so", RTLD_NOW);
	int (*setmax)(uint32_t) = l ? (int (*)(uint32_t))dlsym(l, "ABinderProcess_setThreadPoolMaxThreadCount") : NULL;
	void (*start)(void) = l ? (void (*)(void))dlsym(l, "ABinderProcess_startThreadPool") : NULL;
	if (setmax) setmax(4);
	if (start) start();
}

static unsigned long luma_sum(const uint8_t *y, int stride, int w, int h)
{
	unsigned long s = 0;
	for (int r = 0; r < h; r++)
		for (int x = 0; x < w; x++) s += y[r * stride + x];
	return s;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 3) { fprintf(stderr, "usage: %s <file> <codec> [surface] [seek]\n", argv[0]); return 1; }
	int surface = 0, seek = 0;
	for (int i = 3; i < argc; i++) {
		surface |= !strcmp(argv[i], "surface");
		seek |= !strcmp(argv[i], "seek");
	}
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
	int32_t w = 0, h = 0, stride = 0;
	AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
	AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
	AImageReader *rd = NULL; ANativeWindow *win = NULL;
	if (surface) {
		binder_threads();
		if (AImageReader_newWithUsage(w, h, AIMAGE_FORMAT_YUV_420_888, AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN, 4, &rd) ||
		    AImageReader_getWindow(rd, &win)) { printf("image reader failed\n"); return 1; }
	}
	AMediaCodec *c = strcmp(argv[2], "auto") ? AMediaCodec_createCodecByName(argv[2]) :
			 AMediaCodec_createDecoderByType(mime);
	if (!c) { printf("no codec %s\n", argv[2]); return 1; }
	char *name = NULL;
	if (!AMediaCodec_getName(c, &name)) { printf("codec: %s\n", name); }
	if (AMediaCodec_configure(c, fmt, win, NULL, 0) || AMediaCodec_start(c)) { printf("configure/start failed\n"); return 1; }
	int frames = 0, images = 0, in_eos = 0, out_eos = 0; unsigned long sum = 0, ysum = 0; double t0 = now(), te = 0;
	while (1) {
		if (rd) {
			AImage *img;
			while (AImageReader_acquireNextImage(rd, &img) == AMEDIA_OK) {
				if (!images++) {
					uint8_t *y; int len, rs;
					AImage_getPlaneData(img, 0, &y, &len);
					AImage_getPlaneRowStride(img, 0, &rs);
					ysum = luma_sum(y, rs, w, h);
				}
				AImage_delete(img);
			}
			// the last frames reach the surface after the end of stream
			if (out_eos && (images >= frames || now() - te > 1)) break;
		} else if (out_eos) break;
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
		if (seek && frames == 30) {
			// as a player seeks: flush, then the stream from its start
			seek = 0;
			if (ob >= 0) AMediaCodec_releaseOutputBuffer(c, ob, 0);
			if (AMediaCodec_flush(c)) { printf("flush failed\n"); return 1; }
			AMediaExtractor_seekTo(ex, 0, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
			in_eos = 0;
			printf("flushed after %d frames\n", frames);
			continue;
		}
		if (ob >= 0) {
			int eos = info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM;
			if (info.size > 0 || (surface && !eos)) {
				if (frames == 0 && !surface) {
					size_t sz; uint8_t *o = AMediaCodec_getOutputBuffer(c, ob, &sz);
					for (int i = 0; o && i < info.size; i++) sum += o[info.offset + i];
					if (o) ysum = luma_sum(o + info.offset, stride ? stride : w, w, h);
				}
				frames++;
			}
			AMediaCodec_releaseOutputBuffer(c, ob, surface);
			if (eos) { out_eos = 1; te = now(); }
		} else if (ob == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
			AMediaFormat *of = AMediaCodec_getOutputFormat(c);
			printf("output format: %s\n", AMediaFormat_toString(of));
			AMediaFormat_getInt32(of, AMEDIAFORMAT_KEY_STRIDE, &stride);
			AMediaFormat_delete(of);
		}
		if (now() - t0 > 120) { printf("timeout\n"); break; }
	}
	double t = now() - t0;
	printf("%s: %d frames in %.2f s = %.1f fps, first frame sum %lu, luma %dx%d sum %lu",
	       name ? name : argv[2], frames, t, frames / t, sum, w, h, ysum);
	if (rd) printf(", %d on the surface", images);
	printf("\n");
	AMediaCodec_stop(c); AMediaCodec_delete(c);
	return 0;
}
