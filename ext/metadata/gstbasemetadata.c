/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:metadatabase-metadata
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatabase ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstbasemetadata.h"

#include "metadataexif.h"

#include "metadataiptc.h"

#include "metadataxmp.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_base_metadata_debug);
#define GST_CAT_DEFAULT gst_base_metadata_debug

#define GOTO_DONE_IF_NULL(ptr) do { if ( NULL == (ptr) ) goto done; } while(FALSE)
#define GOTO_DONE_IF_NULL_AND_FAIL(ptr, ret) do { if ( NULL == (ptr) ) { (ret) = FALSE; goto done; } } while(FALSE)

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_EXIF,
  ARG_IPTC,
  ARG_XMP
};

static GstBaseMetadataClass *parent_class = NULL;

static void gst_base_metadata_dispose (GObject * object);

static void gst_base_metadata_finalize (GObject * object);

static void gst_base_metadata_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_base_metadata_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstStateChangeReturn
gst_base_metadata_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_base_metadata_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_base_metadata_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_base_metadata_chain (GstPad * pad, GstBuffer * buf);

static gboolean gst_base_metadata_checkgetrange (GstPad * srcpad);

static GstFlowReturn
gst_base_metadata_get_range (GstPad * pad, guint64 offset_orig, guint size,
    GstBuffer ** buf);

static gboolean gst_base_metadata_sink_activate (GstPad * pad);

static gboolean
gst_base_metadata_src_activate_pull (GstPad * pad, gboolean active);

static gboolean gst_base_metadata_pull_range_base (GstBaseMetadata * filter);

static void gst_base_metadata_init_members (GstBaseMetadata * filter);
static void gst_base_metadata_dispose_members (GstBaseMetadata * filter);

static gboolean gst_base_metadata_configure_caps (GstBaseMetadata * filter);

static int
gst_base_metadata_parse (GstBaseMetadata * filter, const guint8 * buf,
    guint32 size);

static const GstQueryType *gst_base_metadata_get_query_types (GstPad * pad);

static gboolean gst_base_metadata_src_query (GstPad * pad, GstQuery * query);

static void gst_base_metadata_base_init (gpointer gclass);
static void gst_base_metadata_class_init (GstBaseMetadataClass * klass);
static void
gst_base_metadata_init (GstBaseMetadata * filter,
    GstBaseMetadataClass * gclass);

GType
gst_base_metadata_get_type (void)
{
  static GType base_metadata_type = 0;

  if (G_UNLIKELY (base_metadata_type == 0)) {
    static const GTypeInfo base_metadata_info = {
      sizeof (GstBaseMetadataClass),
      (GBaseInitFunc) gst_base_metadata_base_init,
      NULL,
      (GClassInitFunc) gst_base_metadata_class_init,
      NULL,
      NULL,
      sizeof (GstBaseMetadata),
      0,
      (GInstanceInitFunc) gst_base_metadata_init,
    };

    base_metadata_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseMetadata", &base_metadata_info, G_TYPE_FLAG_ABSTRACT);
  }
  return base_metadata_type;
}

/* static helper function */

/*
 * offset - offset of buffer in original stream
 * size - size of buffer
 * seg_offset - offset of segment in original stream
 * seg_size - size of segment
 * boffset - offset inside buffer where segment starts (-1 for no intersection)
 * bsize - size of intersection
 * seg_binter - if segment start inside buffer is zero. if segment start before
 *               buffer and intersect, it is the offset inside segment.
 *
 * ret:
 *  -1 - segment before buffer
 *   0 - segment intersects
 *   1 - segment after buffer
 */

static int
gst_base_metadata_get_strip_seg (const gint64 offset, guint32 size,
    const gint64 seg_offset, const guint32 seg_size,
    gint64 * boffset, guint32 * bsize, guint32 * seg_binter)
{
  int ret = -1;

  *boffset = -1;
  *bsize = 0;
  *seg_binter = -1;

  /* all segment after buffer */
  if (seg_offset >= offset + size) {
    ret = 1;
    goto done;
  }

  if (seg_offset < offset) {
    /* segment start somewhere before buffer */

    /* all segment before buffer */
    if (seg_offset + seg_size <= offset) {
      ret = -1;
      goto done;
    }

    *seg_binter = offset - seg_offset;
    *boffset = 0;

    /* FIXME : optimize to >= size -> = size */
    if (seg_offset + seg_size >= offset + size) {
      /* segment cover all buffer */
      *bsize = size;
    } else {
      /* segment goes from start of buffer to somewhere before end */
      *bsize = seg_size - *seg_binter;
    }

    ret = 0;

  } else {
    /* segment start somewhere into buffer */

    *boffset = seg_offset - offset;
    *seg_binter = 0;

    if (seg_offset + seg_size <= offset + size) {
      /* all segment into buffer */
      *bsize = seg_size;
    } else {
      *bsize = size - *boffset;
    }

    ret = 0;

  }

done:

  return ret;

}

/*
 * extern functions declaration
 */

/*

/*
 *  TRUE -> buffer striped or injeted
 *  FALSE -> buffer unmodified
 */

gboolean
gst_base_metadata_strip_push_buffer (GstBaseMetadata * base,
    gint64 offset_orig, GstBuffer ** prepend, GstBuffer ** buf)
{
  MetadataChunk *strip = META_DATA_STRIP_CHUNKS (base->metadata).chunk;
  MetadataChunk *inject = META_DATA_INJECT_CHUNKS (base->metadata).chunk;
  const gsize strip_len = META_DATA_STRIP_CHUNKS (base->metadata).len;
  const gsize inject_len = META_DATA_INJECT_CHUNKS (base->metadata).len;

  gboolean buffer_reallocated = FALSE;

  guint32 size_buf_in = GST_BUFFER_SIZE (*buf);

  gint64 *boffset_strip = NULL;
  guint32 *bsize_strip = NULL;
  guint32 *seg_binter_strip = NULL;

  int i, j;
  gboolean need_free_strip = FALSE;

  guint32 striped_bytes = 0;
  guint32 injected_bytes = 0;

  guint32 prepend_size = prepend && *prepend ? GST_BUFFER_SIZE (*prepend) : 0;

  if (inject_len) {

    for (i = 0; i < inject_len; ++i) {
      int res;

      if (inject[i].offset_orig >= offset_orig) {
        if (inject[i].offset_orig < offset_orig + size_buf_in) {
          injected_bytes += inject[i].size;
        } else {
          /* segment is after size (segments are sorted) */
          break;
        }
      }
    }

  }

  /*
   * strip segments
   */

  if (strip_len == 0)
    goto inject;

  if (G_UNLIKELY (strip_len > 16)) {
    boffset_strip = g_new (gint64, strip_len);
    bsize_strip = g_new (guint32, strip_len);
    seg_binter_strip = g_new (guint32, strip_len);
    need_free_strip = TRUE;
  } else {
    boffset_strip = g_alloca (sizeof (boffset_strip[0]) * strip_len);
    bsize_strip = g_alloca (sizeof (bsize_strip[0]) * strip_len);
    seg_binter_strip = g_alloca (sizeof (seg_binter_strip[0]) * strip_len);
  }

  memset (bsize_strip, 0x00, sizeof (bsize_strip[0]) * strip_len);

  for (i = 0; i < strip_len; ++i) {
    int res;

    res = gst_base_metadata_get_strip_seg (offset_orig, size_buf_in,
        strip[i].offset_orig, strip[i].size, &boffset_strip[i], &bsize_strip[i],
        &seg_binter_strip[i]);

    /* segment is after size (segments are sorted) */
    striped_bytes += bsize_strip[i];
    if (res > 0) {
      break;
    }

  }

  if (striped_bytes) {

    guint8 *data;

    if (!buffer_reallocated) {
      buffer_reallocated = TRUE;
      if (injected_bytes + prepend_size > striped_bytes) {
        GstBuffer *new_buf =
            gst_buffer_new_and_alloc (GST_BUFFER_SIZE (*buf) + injected_bytes +
            prepend_size - striped_bytes);

        memcpy (GST_BUFFER_DATA (new_buf), GST_BUFFER_DATA (*buf),
            GST_BUFFER_SIZE (*buf));

        gst_buffer_unref (*buf);
        *buf = new_buf;

      } else if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY)) {
        GstBuffer *new_buf = gst_buffer_copy (*buf);

        gst_buffer_unref (*buf);
        *buf = new_buf;
        GST_BUFFER_FLAG_UNSET (*buf, GST_BUFFER_FLAG_READONLY);
        GST_BUFFER_SIZE (*buf) += injected_bytes + prepend_size - striped_bytes;
      }
    }

    data = GST_BUFFER_DATA (*buf);

    striped_bytes = 0;
    for (i = 0; i < strip_len; ++i) {
      /* intersect */
      if (bsize_strip[i]) {
        memmove (data + boffset_strip[i] - striped_bytes,
            data + boffset_strip[i] + bsize_strip[i] - striped_bytes,
            size_buf_in - boffset_strip[i] - bsize_strip[i]);
        striped_bytes += bsize_strip[i];
      }
    }
    size_buf_in -= striped_bytes;

  }

inject:

  /*
   * inject segments
   */

  if (inject_len) {

    guint8 *data;
    guint32 striped_so_far;

    if (!buffer_reallocated) {
      buffer_reallocated = TRUE;
      if (injected_bytes + prepend_size > striped_bytes) {
        GstBuffer *new_buf =
            gst_buffer_new_and_alloc (GST_BUFFER_SIZE (*buf) + injected_bytes +
            prepend_size - striped_bytes);

        memcpy (GST_BUFFER_DATA (new_buf), GST_BUFFER_DATA (*buf),
            GST_BUFFER_SIZE (*buf));

        gst_buffer_unref (*buf);
        *buf = new_buf;

      } else if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_READONLY)) {
        GstBuffer *new_buf = gst_buffer_copy (*buf);

        gst_buffer_unref (*buf);
        *buf = new_buf;
        GST_BUFFER_FLAG_UNSET (*buf, GST_BUFFER_FLAG_READONLY);
        GST_BUFFER_SIZE (*buf) += injected_bytes + prepend_size - striped_bytes;
      }
    }

    data = GST_BUFFER_DATA (*buf);

    injected_bytes = 0;
    striped_so_far = 0;
    j = 0;
    for (i = 0; i < inject_len; ++i) {
      int res;

      while (j < strip_len) {
        if (strip[j].offset_orig < inject[i].offset_orig)
          striped_so_far += bsize_strip[j++];
        else
          break;
      }

      if (inject[i].offset_orig >= offset_orig) {
        if (inject[i].offset_orig <
            offset_orig + size_buf_in + striped_bytes - injected_bytes) {
          /* insert */
          guint32 buf_off =
              inject[i].offset_orig - offset_orig - striped_so_far +
              injected_bytes;
          memmove (data + buf_off + inject[i].size, data + buf_off,
              size_buf_in - buf_off);
          memcpy (data + buf_off, inject[i].data, inject[i].size);
          injected_bytes += inject[i].size;
          size_buf_in += inject[i].size;
        } else {
          /* segment is after size (segments are sorted) */
          break;
        }
      }
    }

  }


done:

  if (prepend_size) {
    if (injected_bytes == 0 && striped_bytes == 0) {
      GstBuffer *new_buf =
          gst_buffer_new_and_alloc (size_buf_in + prepend_size);

      memcpy (GST_BUFFER_DATA (new_buf) + prepend_size, GST_BUFFER_DATA (*buf),
          size_buf_in);

      gst_buffer_unref (*buf);
      *buf = new_buf;
    } else {
      memmove (GST_BUFFER_DATA (*buf) + prepend_size, GST_BUFFER_DATA (*buf),
          size_buf_in);
    }
    memcpy (GST_BUFFER_DATA (*buf), GST_BUFFER_DATA (*prepend), prepend_size);
    gst_buffer_unref (*prepend);
    *prepend = NULL;
  }

  GST_BUFFER_SIZE (*buf) = size_buf_in + prepend_size;

  if (need_free_strip) {
    g_free (boffset_strip);
    g_free (bsize_strip);
    g_free (seg_binter_strip);
  }

  return injected_bytes || striped_bytes;

}

/*
 * pos - position in stream striped
 * orig_pos - position in original stream
 * return TRUE - position in original buffer
 *        FALSE - position in inserted chunk
 */
gboolean
gst_base_metadata_translate_pos_to_orig (GstBaseMetadata * base,
    gint64 pos, gint64 * orig_pos, GstBuffer ** buf)
{
  MetadataChunk *strip = META_DATA_STRIP_CHUNKS (base->metadata).chunk;
  MetadataChunk *inject = META_DATA_INJECT_CHUNKS (base->metadata).chunk;
  const gsize strip_len = META_DATA_STRIP_CHUNKS (base->metadata).len;
  const gsize inject_len = META_DATA_INJECT_CHUNKS (base->metadata).len;
  const gint64 duration_orig = base->duration_orig;
  const gint64 duration = base->duration;

  int i;
  gboolean ret = TRUE;
  guint64 new_buf_size = 0;
  guint64 injected_before = 0;

  if (G_UNLIKELY (pos == -1)) {
    *orig_pos = -1;
    return TRUE;
  } else if (G_UNLIKELY (pos >= duration)) {
    /* this should never happen */
    *orig_pos = duration_orig;
    return TRUE;
  }

  /* calculate for injected */

  /* just calculate size */
  *orig_pos = pos;              /* save pos */
  for (i = 0; i < inject_len; ++i) {
    /* check if pos in inside chunk */
    if (inject[i].offset <= pos) {
      if (pos < inject[i].offset + inject[i].size) {
        /* orig pos points after insert chunk */
        new_buf_size += inject[i].size;
        /* put pos after current chunk */
        pos = inject[i].offset + inject[i].size;
        ret = FALSE;
      } else {
        /* in case pos is not inside a injected chunk */
        injected_before += inject[i].size;
      }
    } else {
      break;
    }
  }

  /* alloc buffer and calcute original pos */
  if (buf && ret == FALSE) {
    guint8 *data;

    if (*buf)
      gst_buffer_unref (*buf);
    *buf = gst_buffer_new_and_alloc (new_buf_size);
    data = GST_BUFFER_DATA (*buf);
    pos = *orig_pos;            /* recover saved pos */
    for (i = 0; i < inject_len; ++i) {
      if (inject[i].offset > pos) {
        break;
      }
      if (inject[i].offset <= pos && pos < inject[i].offset + inject[i].size) {
        memcpy (data, inject[i].data, inject[i].size);
        data += inject[i].size;
        pos = inject[i].offset + inject[i].size;
        /* out position after insert chunk orig */
        *orig_pos = inject[i].offset_orig + inject[i].size;
      }
    }
  }

  if (ret == FALSE) {
    /* if it inside a injected is already done */
    goto done;
  }

  /* calculate for striped */

  *orig_pos = pos - injected_before;
  for (i = 0; i < strip_len; ++i) {
    if (strip[i].offset_orig > pos) {
      break;
    }
    *orig_pos += strip[i].size;
  }

done:

  if (G_UNLIKELY (*orig_pos >= duration_orig)) {
    *orig_pos = duration_orig - 1;
  }

  return ret;

}

/*
 * return:
 *   -1 -> error
 *    0 -> succeded
 *    1 -> need more data
 */

gboolean
gst_base_metadata_calculate_offsets (GstBaseMetadata * base)
{
  int i, j;
  guint32 append_size;
  guint32 bytes_striped, bytes_inject;
  MetadataChunk *strip = META_DATA_STRIP_CHUNKS (base->metadata).chunk;
  MetadataChunk *inject = META_DATA_INJECT_CHUNKS (base->metadata).chunk;
  gsize strip_len;
  gsize inject_len;

  if (base->state != MT_STATE_PARSED)
    return FALSE;

  metadata_lazy_update (base->metadata);

  strip_len = META_DATA_STRIP_CHUNKS (base->metadata).len;
  inject_len = META_DATA_INJECT_CHUNKS (base->metadata).len;

  bytes_striped = 0;
  bytes_inject = 0;

  /* calculate the new position off injected chunks */
  j = 0;
  for (i = 0; i < inject_len; ++i) {
    for (; j < strip_len; ++j) {
      if (strip[j].offset_orig >= inject[i].offset_orig) {
        break;
      }
      bytes_striped += strip[j].size;
    }
    inject[i].offset = inject[i].offset_orig - bytes_striped + bytes_inject;
    bytes_inject += inject[i].size;
  }

  /* calculate append (doesnt make much sense, but, anyway..) */
  append_size = 0;
  for (i = inject_len - 1; i >= 0; --i) {
    if (inject[i].offset_orig == base->duration_orig)
      append_size += inject[i].size;
    else
      break;
  }
  if (append_size) {
    guint8 *data;

    base->append_buffer = gst_buffer_new_and_alloc (append_size);
    GST_BUFFER_FLAG_SET (base->append_buffer, GST_BUFFER_FLAG_READONLY);
    data = GST_BUFFER_DATA (base->append_buffer);
    for (i = inject_len - 1; i >= 0; --i) {
      if (inject[i].offset_orig == base->duration_orig) {
        memcpy (data, inject[i].data, inject[i].size);
        data += inject[i].size;
      } else {
        break;
      }
    }
  }

  if (base->duration_orig) {
    base->duration = base->duration_orig;
    for (i = 0; i < inject_len; ++i) {
      base->duration += inject[i].size;
    }
    for (i = 0; i < strip_len; ++i) {
      base->duration -= strip[i].size;
    }
  }

  return TRUE;

}

const gchar *
gst_base_metadata_get_type_name (int img_type)
{
  gchar *type_name = NULL;

  switch (img_type) {
    case IMG_JPEG:
      type_name = "jpeg";
      break;
    case IMG_PNG:
      type_name = "png";
      break;
    default:
      type_name = "invalid type";
      break;
  }
  return type_name;
}


/* gstreamer functions */


static void
gst_base_metadata_base_init (gpointer gclass)
{
  GST_DEBUG_CATEGORY_INIT (gst_base_metadata_debug, "basemetadata", 0,
      "basemetadata element");
}

/* initialize the plugin's class */
static void
gst_base_metadata_class_init (GstBaseMetadataClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_base_metadata_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_metadata_finalize);

  gobject_class->set_property = gst_base_metadata_set_property;
  gobject_class->get_property = gst_base_metadata_get_property;

  g_object_class_install_property (gobject_class, ARG_EXIF,
      g_param_spec_boolean ("exif", "EXIF", "Send EXIF metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_IPTC,
      g_param_spec_boolean ("iptc", "IPTC", "Send IPTC metadata ?",
          TRUE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_XMP,
      g_param_spec_boolean ("xmp", "XMP", "Send XMP metadata ?",
          TRUE, G_PARAM_READWRITE));

  gstelement_class->change_state = gst_base_metadata_change_state;

}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_base_metadata_init (GstBaseMetadata * filter, GstBaseMetadataClass * gclass)
{
  /* sink pad */

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (gclass), "sink"), "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gclass->set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gclass->get_sink_caps));
  gst_pad_set_event_function (filter->sinkpad, gst_base_metadata_sink_event);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_chain));
  gst_pad_set_activate_function (filter->sinkpad,
      gst_base_metadata_sink_activate);

  /* source pad */

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template
      (GST_ELEMENT_CLASS (gclass), "src"), "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gclass->get_src_caps));
  gst_pad_set_event_function (filter->srcpad, gst_base_metadata_src_event);
  gst_pad_set_query_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_src_query));
  gst_pad_set_query_type_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_get_query_types));
  gst_pad_use_fixed_caps (filter->srcpad);

  gst_pad_set_checkgetrange_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_checkgetrange));
  gst_pad_set_getrange_function (filter->srcpad, gst_base_metadata_get_range);

  gst_pad_set_activatepull_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_base_metadata_src_activate_pull));
  /* addind pads */

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  metadataparse_xmp_init ();
  /* init members */

  gst_base_metadata_init_members (filter);

}

static void
gst_base_metadata_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseMetadata *filter = GST_BASE_METADATA (object);

  switch (prop_id) {
    case ARG_EXIF:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_EXIF;
      else
        filter->options &= ~META_OPT_EXIF;
      break;
    case ARG_IPTC:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_IPTC;
      else
        filter->options &= ~META_OPT_IPTC;
      break;
    case ARG_XMP:
      if (g_value_get_boolean (value))
        filter->options |= META_OPT_XMP;
      else
        filter->options &= ~META_OPT_XMP;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_metadata_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseMetadata *filter = GST_BASE_METADATA (object);

  switch (prop_id) {
    case ARG_EXIF:
      g_value_set_boolean (value, filter->options & META_OPT_EXIF);
      break;
    case ARG_IPTC:
      g_value_set_boolean (value, filter->options & META_OPT_IPTC);
      break;
    case ARG_XMP:
      g_value_set_boolean (value, filter->options & META_OPT_XMP);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

static gboolean
gst_base_metadata_processing (GstBaseMetadata * filter)
{
  gboolean ret = TRUE;
  GstBaseMetadataClass *bclass = GST_BASE_METADATA_GET_CLASS (filter);

  if (filter->need_processing) {
    bclass->processing (filter);
    if (gst_base_metadata_calculate_offsets (filter)) {
      filter->need_processing = FALSE;
    } else {
      ret = FALSE;
    }
  }

  return ret;

}

static gboolean
gst_base_metadata_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret = FALSE;
  GstBaseMetadataClass *bclass;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));
  bclass = GST_BASE_METADATA_GET_CLASS (filter);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      if (filter->need_more_data) {
        GST_ELEMENT_WARNING (filter, STREAM, DECODE, (NULL),
            ("Need more data. Unexpected EOS"));
      }
      break;
    case GST_EVENT_TAG:
      break;
    default:
      break;
  }

  ret = bclass->sink_event (pad, event);

  gst_object_unref (filter);

  return ret;

}


static gboolean
gst_base_metadata_src_event (GstPad * pad, GstEvent * event)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret = FALSE;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type;
      gint64 start;
      GstSeekType stop_type;
      gint64 stop;

      /* we don't know where are the chunks to be stripped before base */
      if (!gst_base_metadata_processing (filter)) {
        ret = FALSE;
        goto done;
      }

      gst_event_parse_seek (event, &rate, &format, &flags,
          &start_type, &start, &stop_type, &stop);

      switch (format) {
        case GST_FORMAT_BYTES:
          break;
        case GST_FORMAT_PERCENT:
          if (filter->duration < 0)
            goto done;
          start = start * filter->duration / 100;
          stop = stop * filter->duration / 100;
          break;
        default:
          goto done;
      }
      format = GST_FORMAT_BYTES;

      if (start_type == GST_SEEK_TYPE_CUR)
        start = filter->offset + start;
      else if (start_type == GST_SEEK_TYPE_END) {
        if (filter->duration < 0)
          goto done;
        start = filter->duration + start;
      }
      start_type == GST_SEEK_TYPE_SET;

      if (filter->prepend_buffer) {
        gst_buffer_unref (filter->prepend_buffer);
        filter->prepend_buffer = NULL;
      }

      /* FIXME: related to append */
      filter->offset = start;
      gst_base_metadata_translate_pos_to_orig (filter, start, &start,
          &filter->prepend_buffer);
      filter->offset_orig = start;

      if (stop_type == GST_SEEK_TYPE_CUR)
        stop = filter->offset + stop;
      else if (stop_type == GST_SEEK_TYPE_END) {
        if (filter->duration < 0)
          goto done;
        stop = filter->duration + stop;
      }
      stop_type == GST_SEEK_TYPE_SET;

      gst_base_metadata_translate_pos_to_orig (filter, stop, &stop, NULL);

      gst_event_unref (event);
      event = gst_event_new_seek (rate, format, flags,
          start_type, start, stop_type, stop);

    }
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);
  event = NULL;                 /* event has another owner */

done:

  if (event) {
    gst_event_unref (event);
  }

  gst_object_unref (filter);

  return ret;

}

static void
gst_base_metadata_dispose (GObject * object)
{
  GstBaseMetadata *filter = NULL;

  filter = GST_BASE_METADATA (object);

  gst_base_metadata_dispose_members (filter);

  metadataparse_xmp_dispose ();

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_base_metadata_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_base_metadata_dispose_members (GstBaseMetadata * filter)
{

  /* buffers used to build output buffer */

  if (filter->prepend_buffer) {
    gst_buffer_unref (filter->prepend_buffer);
    filter->prepend_buffer = NULL;
  }

  if (filter->append_buffer) {
    gst_buffer_unref (filter->append_buffer);
    filter->append_buffer = NULL;
  }

  /* adapter used during parsing process */

  if (filter->adapter_parsing) {
    gst_object_unref (filter->adapter_parsing);
    filter->adapter_parsing = NULL;
  }

  if (filter->adapter_holding) {
    gst_object_unref (filter->adapter_holding);
    filter->adapter_holding = NULL;
  }

  metadata_dispose (&filter->metadata);

}

static void
gst_base_metadata_init_members (GstBaseMetadata * filter)
{

  filter->metadata = NULL;

  filter->img_type = IMG_NONE;

  filter->duration_orig = 0;
  filter->duration = 0;

  filter->state = MT_STATE_NULL;

  filter->options = META_OPT_EXIF | META_OPT_IPTC | META_OPT_XMP;

  filter->need_processing = FALSE;

  filter->adapter_parsing = NULL;
  filter->adapter_holding = NULL;
  filter->next_offset = 0;
  filter->next_size = 0;
  filter->need_more_data = FALSE;
  filter->offset_orig = 0;
  filter->offset = 0;

  filter->append_buffer = NULL;
  filter->prepend_buffer = NULL;

}

static void
gst_base_metadata_reset_streaming (GstBaseMetadata * filter)
{
  filter->offset_orig = 0;
  filter->offset = 0;
  if (filter->adapter_holding)
    gst_adapter_clear (filter->adapter_holding);
}

static void
gst_base_metadata_reset_parsing (GstBaseMetadata * filter)
{


  if (filter->prepend_buffer) {
    gst_buffer_unref (filter->prepend_buffer);
    filter->prepend_buffer = NULL;
  }

  if (filter->append_buffer) {
    gst_buffer_unref (filter->append_buffer);
    filter->append_buffer = NULL;
  }

  if (filter->adapter_parsing)
    gst_adapter_clear (filter->adapter_parsing);

  if (filter->adapter_holding)
    gst_adapter_clear (filter->adapter_holding);

  filter->img_type = IMG_NONE;
  filter->duration_orig = 0;
  filter->duration = 0;
  filter->state = MT_STATE_NULL;
  filter->need_processing = FALSE;
  filter->next_offset = 0;
  filter->next_size = 0;
  filter->need_more_data = FALSE;
  filter->offset_orig = 0;
  filter->offset = 0;

  metadata_dispose (&filter->metadata);

}

static gboolean
gst_base_metadata_configure_caps (GstBaseMetadata * filter)
{
  GstCaps *caps = NULL;
  gboolean ret = FALSE;
  gchar *mime = NULL;
  GstPad *peer = NULL;

  peer = gst_pad_get_peer (filter->sinkpad);

  switch (filter->img_type) {
    case IMG_JPEG:
      mime = "image/jpeg";
      break;
    case IMG_PNG:
      mime = "image/png";
      break;
    default:
      goto done;
      break;
  }

  caps = gst_caps_new_simple (mime, NULL);

  if (!gst_pad_set_caps (peer, caps)) {
    goto done;
  }

  ret = gst_pad_set_caps (filter->sinkpad, caps);

done:

  if (caps) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  if (peer) {
    gst_object_unref (peer);
    peer = NULL;
  }

  return ret;

}

static const GstQueryType *
gst_base_metadata_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_base_metadata_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_FORMATS,
    0
  };

  return gst_base_metadata_src_query_types;
}

static gboolean
gst_base_metadata_src_query (GstPad * pad, GstQuery * query)
{
  gboolean ret = FALSE;
  GstFormat format;
  GstBaseMetadata *filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      gst_query_parse_position (query, &format, NULL);

      if (format == GST_FORMAT_BYTES) {
        gst_query_set_position (query, GST_FORMAT_BYTES, filter->offset);
        ret = TRUE;
      }
      break;
    case GST_QUERY_DURATION:

      if (!gst_base_metadata_processing (filter)) {
        ret = FALSE;
        goto done;
      }

      gst_query_parse_duration (query, &format, NULL);

      if (format == GST_FORMAT_BYTES) {
        if (filter->duration >= 0) {
          gst_query_set_duration (query, GST_FORMAT_BYTES, filter->duration);
          ret = TRUE;
        }
      }
      break;
    case GST_QUERY_FORMATS:
      gst_query_set_formats (query, 1, GST_FORMAT_BYTES);
      ret = TRUE;
      break;
    default:
      break;
  }

done:

  gst_object_unref (filter);

  return ret;

}

/*
 * Do parsing step-by-step and reconfigure caps if need
 * return:
 *   META_PARSING_ERROR
 *   META_PARSING_DONE
 *   META_PARSING_NEED_MORE_DATA
 */

static int
gst_base_metadata_parse (GstBaseMetadata * filter, const guint8 * buf,
    guint32 size)
{

  int ret = META_PARSING_ERROR;

  filter->next_offset = 0;
  filter->next_size = 0;

  ret = metadata_parse (filter->metadata, buf, size,
      &filter->next_offset, &filter->next_size);

  if (ret == META_PARSING_ERROR) {
    if (META_DATA_IMG_TYPE (filter->metadata) == IMG_NONE) {
      /* image type not recognized */
      GST_ELEMENT_ERROR (filter, STREAM, TYPE_NOT_FOUND, (NULL),
          ("Only jpeg and png are supported"));
      goto done;
    }
  } else if (ret == META_PARSING_NEED_MORE_DATA) {
    filter->need_more_data = TRUE;
  } else {
    filter->state = MT_STATE_PARSED;
    filter->need_more_data = FALSE;
    filter->need_processing = TRUE;
  }

  /* reconfigure caps if it is different from type detected by 'base_metadata' function */
  if (filter->img_type != META_DATA_IMG_TYPE (filter->metadata)) {
    filter->img_type = META_DATA_IMG_TYPE (filter->metadata);
    if (!gst_base_metadata_configure_caps (filter)) {
      GST_ELEMENT_ERROR (filter, STREAM, FORMAT, (NULL),
          ("Couldn't reconfigure caps for %s",
              gst_base_metadata_get_type_name (filter->img_type)));
      ret = META_PARSING_ERROR;
      goto done;
    }
  }

done:

  return ret;

}

/* chain function
 * this function does the actual processing
 */

/* FIXME */
/* Current base is just done before is pull mode could be activated */
/* may be it is possible to base in chain mode by doing some trick with gst-adapter */
/* the pipeline below would be a test for that case */
/* gst-launch-0.10 filesrc location=Exif.jpg ! queue !  metadatabase ! filesink location=gen3.jpg */

static GstFlowReturn
gst_base_metadata_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseMetadata *filter = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;
  guint32 buf_size = 0;
  guint32 new_buf_size = 0;
  gboolean append = FALSE;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  if (filter->state != MT_STATE_PARSED) {
    guint32 adpt_size;

    if (G_UNLIKELY (filter->adapter_parsing == NULL))
      filter->adapter_parsing = gst_adapter_new ();

    adpt_size = gst_adapter_available (filter->adapter_parsing);

    if (filter->next_offset) {
      if (filter->next_offset >= adpt_size) {
        /* clean adapter */
        gst_adapter_clear (filter->adapter_parsing);
        filter->next_offset -= adpt_size;
        if (filter->next_offset >= GST_BUFFER_SIZE (buf)) {
          /* we don't need data in this buffer */
          filter->next_offset -= GST_BUFFER_SIZE (buf);
        } else {
          GstBuffer *new_buf;

          /* add to adapter just need part from buf */
          new_buf =
              gst_buffer_new_and_alloc (GST_BUFFER_SIZE (buf) -
              filter->next_offset);
          memcpy (GST_BUFFER_DATA (new_buf),
              GST_BUFFER_DATA (buf) + filter->next_offset,
              GST_BUFFER_SIZE (buf) - filter->next_offset);
          filter->next_offset = 0;
          gst_adapter_push (filter->adapter_parsing, new_buf);
        }
      } else {
        /* remove first bytes and add buffer */
        gst_adapter_flush (filter->adapter_parsing, filter->next_offset);
        filter->next_offset = 0;
        gst_adapter_push (filter->adapter_parsing, gst_buffer_copy (buf));
      }
    } else {
      /* just push buffer */
      gst_adapter_push (filter->adapter_parsing, gst_buffer_copy (buf));
    }

    adpt_size = gst_adapter_available (filter->adapter_parsing);

    if (adpt_size && filter->next_size <= adpt_size) {
      const guint8 *new_buf =
          gst_adapter_peek (filter->adapter_parsing, adpt_size);

      if (gst_base_metadata_parse (filter, new_buf,
              adpt_size) == META_PARSING_ERROR) {
        ret = GST_FLOW_ERROR;
        goto done;
      }
    }
  }

  if (filter->state == MT_STATE_PARSED) {

    if (!gst_base_metadata_processing (filter)) {
      ret = GST_FLOW_ERROR;
      goto done;
    }

    if (filter->adapter_holding) {
      gst_adapter_push (filter->adapter_holding, buf);
      buf = gst_adapter_take_buffer (filter->adapter_holding,
          gst_adapter_available (filter->adapter_holding));
      g_object_unref (filter->adapter_holding);
      filter->adapter_holding = NULL;
    }

    if (filter->offset_orig + GST_BUFFER_SIZE (buf) == filter->duration_orig)
      append = TRUE;

    buf_size = GST_BUFFER_SIZE (buf);

    gst_base_metadata_strip_push_buffer (filter, filter->offset_orig,
        &filter->prepend_buffer, &buf);

    if (buf) {                  /* may be all buffer has been striped */
      gst_buffer_set_caps (buf, GST_PAD_CAPS (filter->srcpad));
      new_buf_size = GST_BUFFER_SIZE (buf);

      ret = gst_pad_push (filter->srcpad, buf);
      buf = NULL;               /* this function don't owner it anymore */
      if (ret != GST_FLOW_OK)
        goto done;
    } else {
      ret = GST_FLOW_OK;
    }

    if (append && filter->append_buffer) {
      gst_buffer_set_caps (filter->append_buffer,
          GST_PAD_CAPS (filter->srcpad));
      gst_buffer_ref (filter->append_buffer);
      ret = gst_pad_push (filter->srcpad, filter->append_buffer);
      if (ret != GST_FLOW_OK)
        goto done;
    }

    filter->offset_orig += buf_size;
    filter->offset += new_buf_size;

  } else {
    /* just store while still not based */
    if (!filter->adapter_holding)
      filter->adapter_holding = gst_adapter_new ();
    gst_adapter_push (filter->adapter_holding, buf);
    buf = NULL;
    ret = GST_FLOW_OK;
  }

done:


  if (buf) {
    /* there was an error and buffer wasn't pushed */
    gst_buffer_unref (buf);
    buf = NULL;
  }

  gst_object_unref (filter);

  return ret;

}

static gboolean
gst_base_metadata_pull_range_base (GstBaseMetadata * filter)
{

  int res;
  gboolean ret = TRUE;
  guint32 offset = 0;
  gint64 duration = 0;
  GstFormat format = GST_FORMAT_BYTES;

  if (!(ret =
          gst_pad_query_peer_duration (filter->sinkpad, &format, &duration))) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }
  filter->duration_orig = duration;

  if (format != GST_FORMAT_BYTES) {
    /* this should never happen, but try chain anyway */
    ret = TRUE;
    goto done;
  }

  do {
    GstFlowReturn flow;
    GstBuffer *buf = NULL;

    offset += filter->next_offset;

    /* 'filter->next_size' only says the minimum required number of bytes.
       We try provided more bytes (4096) just to avoid a lot of calls to 'metadata_parse'
       returning META_PARSING_NEED_MORE_DATA */
    if (filter->next_size < 4096) {
      if (duration - offset < 4096) {
        /* In case there is no 4096 bytes available upstream.
           It should be done upstream but we do here for safety */
        filter->next_size = duration - offset;
      } else {
        filter->next_size = 4096;
      }
    }

    flow =
        gst_pad_pull_range (filter->sinkpad, offset, filter->next_size, &buf);
    if (GST_FLOW_OK != flow) {
      ret = FALSE;
      goto done;
    }

    res =
        gst_base_metadata_parse (filter, GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    if (res == META_PARSING_ERROR) {
      ret = FALSE;
      goto done;
    }

    gst_buffer_unref (buf);

  } while (res == META_PARSING_NEED_MORE_DATA);

done:

  return ret;

}

static gboolean
gst_base_metadata_sink_activate (GstPad * pad)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret = TRUE;


  filter = GST_BASE_METADATA (GST_PAD_PARENT (pad));

  if (!gst_pad_check_pull_range (pad) ||
      !gst_pad_activate_pull (filter->sinkpad, TRUE)) {
    /* FIXME: currently it is not possible to base in chain. Fail here ? */
    /* nothing to be done by now, activate push mode */
    return gst_pad_activate_push (pad, TRUE);
  }

  /* try to base */
  if (filter->state == MT_STATE_NULL) {
    ret = gst_base_metadata_pull_range_base (filter);
  }

done:

  if (ret) {
    gst_pad_activate_pull (pad, FALSE);
    gst_pad_activate_push (filter->srcpad, FALSE);
    if (!gst_pad_is_active (pad)) {
      ret = gst_pad_activate_push (filter->srcpad, TRUE);
      ret = ret && gst_pad_activate_push (pad, TRUE);
    }
  }

  return ret;

}

static gboolean
gst_base_metadata_checkgetrange (GstPad * srcpad)
{
  GstBaseMetadata *filter = NULL;

  filter = GST_BASE_METADATA (GST_PAD_PARENT (srcpad));

  return gst_pad_check_pull_range (filter->sinkpad);
}

static GstFlowReturn
gst_base_metadata_get_range (GstPad * pad,
    guint64 offset, guint size, GstBuffer ** buf)
{
  GstBaseMetadata *filter = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  gint64 offset_orig = 0;
  guint size_orig;
  GstBuffer *prepend = NULL;
  gboolean need_append = FALSE;

  filter = GST_BASE_METADATA (GST_PAD_PARENT (pad));

  if (!gst_base_metadata_processing (filter)) {
    ret = GST_FLOW_ERROR;
    goto done;
  }

  if (offset + size > filter->duration) {
    size = filter->duration - offset;
  }

  size_orig = size;

  gst_base_metadata_translate_pos_to_orig (filter, offset,
      &offset_orig, &prepend);

  if (size > 1) {
    gint64 pos;

    pos = offset + size - 1;
    gst_base_metadata_translate_pos_to_orig (filter, pos, &pos, NULL);
    size_orig = pos + 1 - offset_orig;
  }

  if (size_orig) {

    ret = gst_pad_pull_range (filter->sinkpad, offset_orig, size_orig, buf);

    if (ret == GST_FLOW_OK && *buf) {
      gst_base_metadata_strip_push_buffer (filter, offset_orig, &prepend, buf);

      if (GST_BUFFER_SIZE (*buf) < size) {
        /* need append */
        need_append = TRUE;
      }

    }
  } else {
    *buf = prepend;
  }

done:

  if (need_append) {
    /* FIXME: together with SEEK and
     * gst_base_metadata_translate_pos_to_orig
     * this way if chunk is added in the end we are in trolble
     * ...still not implemented 'cause it will not be the
     * case for the time being
     */
  }

  return ret;

}

static gboolean
gst_base_metadata_src_activate_pull (GstPad * pad, gboolean active)
{
  GstBaseMetadata *filter = NULL;
  gboolean ret;

  filter = GST_BASE_METADATA (gst_pad_get_parent (pad));

  ret = gst_pad_activate_pull (filter->sinkpad, active);

  if (ret && filter->state == MT_STATE_NULL) {
    ret = gst_base_metadata_pull_range_base (filter);
  }

  gst_object_unref (filter);

  return ret;
}


static GstStateChangeReturn
gst_base_metadata_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstBaseMetadata *filter = GST_BASE_METADATA (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_base_metadata_reset_parsing (filter);
      metadata_init (&filter->metadata, filter->options);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (filter->metadata == NULL)
        metadata_init (&filter->metadata, filter->options);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    goto done;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_base_metadata_reset_streaming (filter);
      if (filter->state != MT_STATE_PARSED)
        gst_base_metadata_reset_parsing (filter);
      break;
    default:
      break;
  }

done:

  return ret;
}

void
gst_base_metadata_set_option_flag (GstBaseMetadata * base, MetaOptions options)
{
  base->options |= options;
}

void
gst_base_metadata_unset_option_flag (GstBaseMetadata * base,
    MetaOptions options)
{
  base->options &= ~options;
}

MetaOptions
gst_base_metadata_get_option_flag (const GstBaseMetadata * base)
{
  return base->options;
}

void
gst_base_metadata_update_segment_with_new_buffer (GstBaseMetadata * base,
    guint8 ** buf, guint32 * size, MetadataChunkType type)
{
  int i;
  MetadataChunk *inject = META_DATA_INJECT_CHUNKS (base->metadata).chunk;
  const gsize inject_len = META_DATA_INJECT_CHUNKS (base->metadata).len;

  if (!(buf && size))
    goto done;
  if (*buf == 0)
    goto done;
  if (*size == 0)
    goto done;

  for (i = 0; i < inject_len; ++i) {
    if (inject[i].type == type) {
      inject[i].size = *size;
      if (inject[i].data)
        g_free (inject[i].data);
      inject[i].data = *buf;
      *size = 0;
      *buf = 0;
      break;
    }
  }

done:

  return;

}

void
gst_base_metadata_chunk_array_remove_zero_size (GstBaseMetadata * base)
{
  metadata_chunk_array_remove_zero_size (&META_DATA_INJECT_CHUNKS (base->
          metadata));
}