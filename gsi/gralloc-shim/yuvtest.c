// Imports a YUV 4:2:0 AHardwareBuffer into EGL/GLES (external texture), draws
// it into an RGBA framebuffer and reads one pixel: Y=200, U=V=128 should come
// back as a light grey (about 213 with limited-range BT.601).
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#include <stdio.h>
#include <string.h>

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

int main(void)
{
	AHardwareBuffer_Desc d = { .width = 64, .height = 64, .layers = 1,
		.format = AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420,
		.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN };
	AHardwareBuffer *b;
	AHardwareBuffer_Planes p;
	if (AHardwareBuffer_allocate(&d, &b)) { printf("allocate failed\n"); return 1; }
	if (AHardwareBuffer_lockPlanes(b, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, NULL, &p)) { printf("lock failed\n"); return 1; }
	for (int y = 0; y < 64; y++) memset((char *)p.planes[0].data + y * p.planes[0].rowStride, 200, 64);
	for (int pl = 1; pl < 3; pl++)
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++)
				((unsigned char *)p.planes[pl].data)[y * p.planes[pl].rowStride + x * p.planes[pl].pixelStride] = 128;
	AHardwareBuffer_unlock(b, NULL);

	EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(dpy, NULL, NULL);
	EGLint ca[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RED_SIZE, 8, EGL_NONE };
	EGLConfig cfg; EGLint n;
	eglChooseConfig(dpy, ca, &cfg, 1, &n);
	EGLint sa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
	EGLSurface s = eglCreatePbufferSurface(dpy, cfg, sa);
	EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
	eglMakeCurrent(dpy, s, s, c);
	printf("GL: %s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	EGLClientBuffer cb = eglGetNativeClientBufferANDROID(b);
	EGLint ia[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
	EGLImageKHR img = eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, cb, ia);
	if (img == EGL_NO_IMAGE_KHR) { printf("YUV EGLImage FAILED: 0x%x\n", eglGetError()); return 2; }
	GLuint t; glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, t);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, img);
	GLuint pr = glCreateProgram();
	glAttachShader(pr, shader(GL_VERTEX_SHADER, "attribute vec2 a; varying vec2 v; void main(){v=a*0.5+0.5; gl_Position=vec4(a,0.,1.);}"));
	glAttachShader(pr, shader(GL_FRAGMENT_SHADER, "#extension GL_OES_EGL_image_external : require\nprecision mediump float; varying vec2 v; uniform samplerExternalOES s; void main(){gl_FragColor=texture2D(s,v);}"));
	glBindAttribLocation(pr, 0, "a");
	glLinkProgram(pr); glUseProgram(pr);
	static const float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[4];
	glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("YUV import OK, pixel = %d %d %d %d (expect ~213 213 213), GL error 0x%x\n", px[0], px[1], px[2], px[3], glGetError());
	return 0;
}
