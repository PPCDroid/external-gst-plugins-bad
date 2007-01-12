/* GStreamer
 *
 * Copyright (C) 2006 Lutz Mueller <lutz@topfrose.de>
 *		 2006 Edward Hervey <bilboed@bilbod.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_REAL_VIDEO_DEC_H__
#define __GST_REAL_VIDEO_DEC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_REAL_VIDEO_DEC (gst_real_video_dec_get_type())
#define GST_REAL_VIDEO_DEC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_REAL_VIDEO_DEC,GstRealVideoDec))
#define GST_REAL_VIDEO_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_REAL_VIDEO_DEC,GstRealVideoDecClass))

typedef struct _GstRealVideoDec GstRealVideoDec;
typedef struct _GstRealVideoDecClass GstRealVideoDecClass;
typedef enum _GstRealVideoDecVersion GstRealVideoDecVersion;

enum _GstRealVideoDecVersion
{
  GST_REAL_VIDEO_DEC_VERSION_2 = 2,
  GST_REAL_VIDEO_DEC_VERSION_3 = 3,
  GST_REAL_VIDEO_DEC_VERSION_4 = 4
};

typedef struct {
  gpointer handle;

  guint32 (*custom_message) (gpointer, gpointer);
  guint32 (*free) (gpointer);
  guint32 (*init) (gpointer, gpointer);
  guint32 (*transform) (gchar *, gchar *, gpointer, gpointer, gpointer);

  gpointer context;
} GstRealVideoDecHooks;

struct _GstRealVideoDec
{
  GstElement parent;

  GstPad *src, *snk;

  /* Caps */
  GstRealVideoDecVersion version;
  guint width, height;
  gint format, subformat;
  gint framerate_num, framerate_denom;

  /* Variables needed for fixing timestamps. */
  GstClockTime next_ts, last_ts;
  guint16 next_seq, last_seq;

  /* Hooks */
  GstRealVideoDecHooks hooks;

  /* State */
  GstAdapter *adapter;
  guint8 seqnum, subseq;
  guint16 length;
  guint32 fragment_count;
  guint32 fragments[256];

  /* Properties */
  gchar *path_rv20, *path_rv30, *path_rv40;
};

struct _GstRealVideoDecClass
{
  GstElementClass parent_class;
};

GType gst_real_video_dec_get_type (void);

G_END_DECLS
#endif /* __GST_REAL_VIDEO_DEC_H__ */