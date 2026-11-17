// Streams YUV_420_888 from each camera through the NDK camera API for a few
// seconds and reports the frame rate and the average luma of the frames (no
// picture is kept), then takes one JPEG at the largest size and reports its
// size. usage: camtest [seconds] [width height] [yuv|jpeg|both]
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraMetadata.h>
#include <camera/NdkCameraMetadataTags.h>
#include <dlfcn.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

// the camera service calls back into this process: binder threads
static void binder_threads(void)
{
	void *l = dlopen("libbinder_ndk.so", RTLD_NOW);
	int (*setmax)(uint32_t) = l ? (int (*)(uint32_t))dlsym(l, "ABinderProcess_setThreadPoolMaxThreadCount") : NULL;
	void (*start)(void) = l ? (void (*)(void))dlsym(l, "ABinderProcess_startThreadPool") : NULL;
	if (setmax) setmax(4);
	if (start) start();
}

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int frames, jpegs, jpeg_bytes;
static double first_t, last_t, luma_sum;

static void on_yuv(void *ctx, AImageReader *r)
{
	(void)ctx;
	AImage *img = NULL;
	if (AImageReader_acquireLatestImage(r, &img) != AMEDIA_OK || !img)
		return;
	uint8_t *y = NULL;
	int len = 0, stride = 0, w = 0, h = 0;
	AImage_getPlaneData(img, 0, &y, &len);
	AImage_getPlaneRowStride(img, 0, &stride);
	AImage_getWidth(img, &w);
	AImage_getHeight(img, &h);
	unsigned long s = 0, n = 0;
	for (int r2 = 0; y && r2 < h; r2 += 8)
		for (int x = 0; x < w; x += 8, n++)
			s += y[r2 * stride + x];
	pthread_mutex_lock(&lock);
	if (!frames) first_t = now();
	last_t = now();
	frames++;
	luma_sum = n ? (double)s / n : 0;
	pthread_mutex_unlock(&lock);
	AImage_delete(img);
}

static void on_jpeg(void *ctx, AImageReader *r)
{
	(void)ctx;
	AImage *img = NULL;
	if (AImageReader_acquireNextImage(r, &img) != AMEDIA_OK || !img)
		return;
	uint8_t *d = NULL;
	int len = 0;
	AImage_getPlaneData(img, 0, &d, &len);
	// a JPEG in a BLOB buffer ends with FF D9; find its real length
	int end = len;
	for (int i = 2; i < len - 1; i++)
		if (d[i] == 0xff && d[i + 1] == 0xd9) { end = i + 2; }
	pthread_mutex_lock(&lock);
	jpegs++;
	jpeg_bytes = (d && len > 2 && d[0] == 0xff && d[1] == 0xd8) ? end : -len;
	pthread_mutex_unlock(&lock);
	AImage_delete(img);
}

static void dev_disconnected(void *c, ACameraDevice *d) { (void)c; (void)d; printf("  device disconnected\n"); }
static void dev_error(void *c, ACameraDevice *d, int e) { (void)c; (void)d; printf("  device error %d\n", e); }
static void s_closed(void *c, ACameraCaptureSession *s) { (void)c; (void)s; }
static void s_ready(void *c, ACameraCaptureSession *s) { (void)c; (void)s; }
static void s_active(void *c, ACameraCaptureSession *s) { (void)c; (void)s; }

static int largest_jpeg(ACameraMetadata *ch, int *w, int *h)
{
	ACameraMetadata_const_entry e;
	*w = *h = 0;
	if (ACameraMetadata_getConstEntry(ch, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &e) != ACAMERA_OK)
		return -1;
	for (uint32_t i = 0; i + 3 < e.count; i += 4)
		if (e.data.i32[i] == AIMAGE_FORMAT_JPEG && !e.data.i32[i + 3] &&
		    e.data.i32[i + 1] * e.data.i32[i + 2] > *w * *h) {
			*w = e.data.i32[i + 1];
			*h = e.data.i32[i + 2];
		}
	return *w ? 0 : -1;
}

static int want_yuv = 1, want_jpeg = 1;

static void test(ACameraManager *m, const char *id, double seconds, int w, int h)
{
	ACameraMetadata *ch = NULL;
	ACameraManager_getCameraCharacteristics(m, id, &ch);
	ACameraMetadata_const_entry e;
	const char *facing = "?";
	if (ch && ACameraMetadata_getConstEntry(ch, ACAMERA_LENS_FACING, &e) == ACAMERA_OK)
		facing = e.data.u8[0] == ACAMERA_LENS_FACING_FRONT ? "front" : e.data.u8[0] == ACAMERA_LENS_FACING_BACK ? "back" : "external";
	int hw = -1;
	if (ch && ACameraMetadata_getConstEntry(ch, ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL, &e) == ACAMERA_OK)
		hw = e.data.u8[0];
	int jw = 0, jh = 0;
	if (ch && want_jpeg) largest_jpeg(ch, &jw, &jh);
	printf("camera %s: %s, hardware level %d, largest JPEG %dx%d\n", id, facing, hw, jw, jh);

	ACameraDevice_StateCallbacks dcb = { .onDisconnected = dev_disconnected, .onError = dev_error };
	ACameraDevice *dev = NULL;
	camera_status_t st = ACameraManager_openCamera(m, id, &dcb, &dev);
	if (st != ACAMERA_OK) { printf("  openCamera: %d\n", st); return; }

	AImageReader *yr = NULL, *jr = NULL;
	AImageReader_new(w, h, AIMAGE_FORMAT_YUV_420_888, 4, &yr);
	AImageReader_ImageListener yl = { .onImageAvailable = on_yuv };
	AImageReader_setImageListener(yr, &yl);
	ANativeWindow *yw = NULL;
	AImageReader_getWindow(yr, &yw);
	if (jw) {
		AImageReader_new(jw, jh, AIMAGE_FORMAT_JPEG, 2, &jr);
		AImageReader_ImageListener jl = { .onImageAvailable = on_jpeg };
		AImageReader_setImageListener(jr, &jl);
	}
	ANativeWindow *jwin = NULL;
	if (jr) AImageReader_getWindow(jr, &jwin);

	ACaptureSessionOutputContainer *outs = NULL;
	ACaptureSessionOutputContainer_create(&outs);
	ACaptureSessionOutput *yo = NULL, *jo = NULL;
	if (want_yuv) {
		ACaptureSessionOutput_create(yw, &yo);
		ACaptureSessionOutputContainer_add(outs, yo);
	}
	if (jwin) { ACaptureSessionOutput_create(jwin, &jo); ACaptureSessionOutputContainer_add(outs, jo); }
	ACameraCaptureSession_stateCallbacks scb = { .onClosed = s_closed, .onReady = s_ready, .onActive = s_active };
	ACameraCaptureSession *sess = NULL;
	st = ACameraDevice_createCaptureSession(dev, outs, &scb, &sess);
	if (st != ACAMERA_OK) { printf("  createCaptureSession: %d\n", st); goto out; }

	if (!want_yuv)
		goto still;
	ACaptureRequest *req = NULL;
	ACameraDevice_createCaptureRequest(dev, TEMPLATE_PREVIEW, &req);
	ACameraOutputTarget *yt = NULL;
	ACameraOutputTarget_create(yw, &yt);
	ACaptureRequest_addTarget(req, yt);
	frames = 0;
	double t0 = now();
	st = ACameraCaptureSession_setRepeatingRequest(sess, NULL, 1, &req, NULL);
	if (st != ACAMERA_OK) { printf("  setRepeatingRequest: %d\n", st); goto out; }
	for (int tick = 0; now() - t0 < seconds; tick++) {
		usleep(100000);
		if (tick % 20 == 19) {
			pthread_mutex_lock(&lock);
			printf("  %4.1f s: %d frames, luma %.0f\n", now() - t0, frames, luma_sum);
			pthread_mutex_unlock(&lock);
		}
	}
	ACameraCaptureSession_stopRepeating(sess);
	pthread_mutex_lock(&lock);
	printf("  YUV %dx%d: %d frames, first after %.2f s, %.1f fps, average luma of the last %.0f\n", w, h, frames,
	       frames ? first_t - t0 : 0, frames > 1 ? (frames - 1) / (last_t - first_t) : 0, luma_sum);
	pthread_mutex_unlock(&lock);

still:
	if (jwin) {
		ACaptureRequest *jreq = NULL;
		ACameraDevice_createCaptureRequest(dev, TEMPLATE_STILL_CAPTURE, &jreq);
		ACameraOutputTarget *jt = NULL;
		ACameraOutputTarget_create(jwin, &jt);
		ACaptureRequest_addTarget(jreq, jt);
		jpegs = 0;
		double tj = now();
		st = ACameraCaptureSession_capture(sess, NULL, 1, &jreq, NULL);
		while (st == ACAMERA_OK && !jpegs && now() - tj < 15) usleep(50000);
		printf("  JPEG %dx%d: %s, %d bytes, %.2f s\n", jw, jh, jpegs ? "taken" : "none", jpeg_bytes, now() - tj);
	}
out:
	if (sess) ACameraCaptureSession_close(sess);
	ACameraDevice_close(dev);
	if (yr) AImageReader_delete(yr);
	if (jr) AImageReader_delete(jr);
	if (ch) ACameraMetadata_free(ch);
}

int main(int argc, char **argv)
{
	double seconds = argc > 1 ? atof(argv[1]) : 5;
	int w = argc > 3 ? atoi(argv[2]) : 1280, h = argc > 3 ? atoi(argv[3]) : 720;
	if (argc > 4) {
		want_yuv = strcmp(argv[4], "jpeg") != 0;
		want_jpeg = strcmp(argv[4], "yuv") != 0;
	}
	binder_threads();
	ACameraManager *m = ACameraManager_create();
	ACameraIdList *ids = NULL;
	if (ACameraManager_getCameraIdList(m, &ids) != ACAMERA_OK || !ids) { printf("no camera list\n"); return 1; }
	printf("%d camera(s)\n", ids->numCameras);
	for (int i = 0; i < ids->numCameras; i++)
		test(m, ids->cameraIds[i], seconds, w, h);
	ACameraManager_deleteCameraIdList(ids);
	ACameraManager_delete(m);
	return 0;
}
