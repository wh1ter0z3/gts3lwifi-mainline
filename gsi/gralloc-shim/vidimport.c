// Decodes the first frames of a video with a named decoder into an ImageReader
// that has SurfaceFlinger's consumer usage (GPU sampled, composer overlay),
// then imports a frame's buffer into EGL/GLES as SurfaceFlinger's RenderEngine
// does (an EGLImage of the native buffer, an external texture), draws it and
// reads back a few pixels: whether video can be composited, per decoder.
// usage: vidimport <file> <codec name>   (EGL_LOG_LEVEL=debug for Mesa's view)
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FRAME 10	/* the frame to look at (the first ones may be held) */

static void binder_threads(void)
{
	void *l = dlopen("libbinder_ndk.so", RTLD_NOW);
	int (*setmax)(uint32_t) = l ? (int (*)(uint32_t))dlsym(l, "ABinderProcess_setThreadPoolMaxThreadCount") : NULL;
	void (*start)(void) = l ? (void (*)(void))dlsym(l, "ABinderProcess_startThreadPool") : NULL;
	if (setmax) setmax(4);
	if (start) start();
}

static GLuint shader(GLenum t, const char *s)
{
	GLuint sh = glCreateShader(t);
	GLint ok;
	glShaderSource(sh, 1, &s, NULL);
	glCompileShader(sh);
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) { char l[512]; glGetShaderInfoLog(sh, 512, NULL, l); printf("shader: %s\n", l); }
	return sh;
}

static int composite(AHardwareBuffer *b)
{
	AHardwareBuffer_Desc d;
	AHardwareBuffer_describe(b, &d);
	printf("buffer %ux%u format 0x%x usage 0x%llx stride %u\n", d.width, d.height, d.format,
	       (unsigned long long)d.usage, d.stride);

	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(dpy, NULL, NULL);
	EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_NONE };
	EGLConfig cfg; EGLint n;
	eglChooseConfig(dpy, ca, &cfg, 1, &n);
	EGLint sa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 36, EGL_NONE };
	EGLSurface s = eglCreatePbufferSurface(dpy, cfg, sa);
	EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
	eglMakeCurrent(dpy, s, s, c);

	EGLClientBuffer cb = eglGetNativeClientBufferANDROID(b);
	EGLint ia[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
	EGLImageKHR img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb, ia);
	if (img == EGL_NO_IMAGE_KHR) { printf("EGLImage FAILED: 0x%x\n", eglGetError()); return 2; }
	GLuint t; glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, img);
	printf("EGLImage ok, texture GL error 0x%x\n", glGetError());
	GLuint pr = glCreateProgram();
	glAttachShader(pr, shader(GL_VERTEX_SHADER, "attribute vec2 a; varying vec2 v; void main(){v=vec2(a.x*0.5+0.5, 0.5-a.y*0.5); gl_Position=vec4(a,0.,1.);}"));
	glAttachShader(pr, shader(GL_FRAGMENT_SHADER, "#extension GL_OES_EGL_image_external : require\nprecision mediump float; varying vec2 v; uniform samplerExternalOES s; void main(){gl_FragColor=texture2D(s,v);}"));
	glBindAttribLocation(pr, 0, "a");
	glLinkProgram(pr); glUseProgram(pr);
	static const float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
	glClearColor(1, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[64 * 36 * 4];
	glReadPixels(0, 0, 64, 36, GL_RGBA, GL_UNSIGNED_BYTE, px);
	unsigned long sum[3] = { 0 };
	for (int i = 0; i < 64 * 36; i++) for (int k = 0; k < 3; k++) sum[k] += px[i * 4 + k];
	printf("drawn 64x36: mean RGB %lu %lu %lu; pixels (8,8) %d %d %d, (32,18) %d %d %d; GL error 0x%x\n",
	       sum[0] / 2304, sum[1] / 2304, sum[2] / 2304,
	       px[(8 * 64 + 8) * 4], px[(8 * 64 + 8) * 4 + 1], px[(8 * 64 + 8) * 4 + 2],
	       px[(18 * 64 + 32) * 4], px[(18 * 64 + 32) * 4 + 1], px[(18 * 64 + 32) * 4 + 2], glGetError());
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc < 3) { fprintf(stderr, "usage: %s <file> <codec>\n", argv[0]); return 1; }
	int fd = open(argv[1], O_RDONLY); struct stat st; fstat(fd, &st);
	AMediaExtractor *ex = AMediaExtractor_new();
	if (AMediaExtractor_setDataSourceFd(ex, fd, 0, st.st_size)) { printf("extractor failed\n"); return 1; }
	AMediaFormat *fmt = NULL; const char *mime = NULL; int track = -1;
	for (size_t i = 0; i < AMediaExtractor_getTrackCount(ex); i++) {
		fmt = AMediaExtractor_getTrackFormat(ex, i);
		if (AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && !strncmp(mime, "video/", 6)) { track = i; break; }
	}
	if (track < 0) { printf("no video track\n"); return 1; }
	AMediaExtractor_selectTrack(ex, track);
	int32_t w = 0, h = 0;
	AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
	AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);

	binder_threads();
	AImageReader *rd; ANativeWindow *win;
	if (AImageReader_newWithUsage(w, h, AIMAGE_FORMAT_PRIVATE,
				      AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY,
				      4, &rd) || AImageReader_getWindow(rd, &win)) { printf("image reader failed\n"); return 1; }
	AMediaCodec *c = AMediaCodec_createCodecByName(argv[2]);
	if (!c) { printf("no codec %s\n", argv[2]); return 1; }
	if (AMediaCodec_configure(c, fmt, win, NULL, 0) || AMediaCodec_start(c)) { printf("configure/start failed\n"); return 1; }

	int frames = 0, images = 0;
	for (int loop = 0; loop < 2000 && images <= FRAME; loop++) {
		ssize_t ib = AMediaCodec_dequeueInputBuffer(c, 5000);
		if (ib >= 0) {
			size_t cap; uint8_t *buf = AMediaCodec_getInputBuffer(c, ib, &cap);
			ssize_t n = AMediaExtractor_readSampleData(ex, buf, cap);
			if (n < 0) AMediaCodec_queueInputBuffer(c, ib, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
			else { AMediaCodec_queueInputBuffer(c, ib, 0, n, AMediaExtractor_getSampleTime(ex), 0); AMediaExtractor_advance(ex); }
		}
		AMediaCodecBufferInfo info;
		ssize_t ob = AMediaCodec_dequeueOutputBuffer(c, &info, 5000);
		if (ob >= 0) { AMediaCodec_releaseOutputBuffer(c, ob, 1); frames++; }
		AImage *img;
		while (AImageReader_acquireNextImage(rd, &img) == AMEDIA_OK) {
			if (images++ == FRAME) {
				AHardwareBuffer *b;
				if (AImage_getHardwareBuffer(img, &b)) { printf("no hardware buffer\n"); return 1; }
				AHardwareBuffer_acquire(b);
				AImage_delete(img);
				printf("%s: frame %d\n", argv[2], FRAME);
				int r = composite(b);
				_exit(r);
			}
			AImage_delete(img);
		}
	}
	printf("%s: only %d frames, %d images\n", argv[2], frames, images);
	_exit(1);
}
