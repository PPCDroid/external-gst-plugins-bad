LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	gstfbdevsink.c

LOCAL_SHARED_LIBRARIES :=	\
	libgstvideo-0.10	\
	libgstreamer-0.10	\
	libgstbase-0.10		\
	libglib-2.0		\
	libgthread-2.0		\
	libgmodule-2.0		\
	libgobject-2.0

LOCAL_MODULE:= libgstfbdevsink

LOCAL_C_INCLUDES := 			\
	$(LOCAL_PATH)			\
	$(GST_PLUGINS_BAD_TOP)		\
	$(GST_PLUGINS_BAD_TOP)/android	\
	external/gstreamer		\
	external/gstreamer/android/arch/$(TARGET_ARCH) 	\
	external/gstreamer/libs		\
	external/gstreamer/gst		\
	external/gstreamer/gst/android	\
	external/gst-plugins-base/gst-libs   \
	external/gst-plugins-base/gst-libs/gst/video/android   \
	external/glib			\
	external/glib/android	  	\
	external/glib/glib		\
	external/glib/gmodule	  	\
	external/glib/gobject	  	\
	external/glib/gthread

LOCAL_CFLAGS := \
	-DHAVE_CONFIG_H		\
	-DBUILD_WITH_ANDROID

include $(BUILD_PLUGIN_LIBRARY)
