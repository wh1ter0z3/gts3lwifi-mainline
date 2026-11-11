# Open libQSEEComAPI for mainline Linux (see QSEEComAPI.c)
CC ?= cc
CFLAGS ?= -O2 -Wall -fPIC
PREFIX ?= /usr/local

all: libQSEEComAPI.so qseecom-km

libQSEEComAPI.so: QSEEComAPI.c QSEEComAPI.h
	$(CC) $(CFLAGS) -shared -Wl,-soname,libQSEEComAPI.so -o $@ QSEEComAPI.c

# a client that uses the API the way Samsung's keymaster HAL does
qseecom-km: qseecom-km.c QSEEComAPI.h libQSEEComAPI.so
	$(CC) $(CFLAGS) -o $@ qseecom-km.c -L. -lQSEEComAPI -Wl,-rpath,'$$ORIGIN'

install: libQSEEComAPI.so
	install -Dm755 libQSEEComAPI.so $(DESTDIR)$(PREFIX)/lib/libQSEEComAPI.so
	install -Dm644 QSEEComAPI.h $(DESTDIR)$(PREFIX)/include/QSEEComAPI.h

clean:
	rm -f libQSEEComAPI.so qseecom-km
