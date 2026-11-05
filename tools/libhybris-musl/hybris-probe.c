#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv)
{
	void *h = dlopen("libhybris-common.so.1", RTLD_NOW);
	void *(*adlopen)(const char *, int) = h ? dlsym(h, "android_dlopen") : NULL;
	const char *(*adlerror)(void) = h ? dlsym(h, "android_dlerror") : NULL;
	if (!adlopen) { fprintf(stderr, "no hybris: %s\n", dlerror()); return 1; }
	for (int i = 1; i < argc; i++) {
		printf("loading %s... ", argv[i]); fflush(stdout);
		void *a = adlopen(argv[i], 2);
		printf("%s\n", a ? "ok" : (adlerror ? adlerror() : "FAILED")); fflush(stdout);
	}
	return 0;
}
