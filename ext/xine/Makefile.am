plugin_LTLIBRARIES = libgstxine.la

libgstxine_la_SOURCES = \
	xine.c		\
	xineaudiodec.c	\
	xineaudiosink.c	\
	xinecaps.c	\
	xineinput.c
libgstxine_la_CFLAGS = $(GST_CFLAGS) $(XINE_CFLAGS)
libgstxine_la_LIBADD = $(XINE_LIBS)
libgstxine_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstxine_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstxine.h
