/* GStreamer pitch controller element
 * Copyright (C) 2006 Wouter Paesen <wouter@blue-gate.be>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include "gstpitch.hh"
#include <math.h>

#define FLOAT_SAMPLES
#include <soundtouch/SoundTouch.h>

/* wtf ?
#ifdef G_PARAM_READWRITE
#  undef G_PARAM_READWRITE
#endif
#define G_PARAM_READWRITE ((GParamFlags)(G_PARAM_READABLE | G_PARAM_WRITABLE))
*/

GST_DEBUG_CATEGORY_STATIC (pitch_debug);
#define GST_CAT_DEFAULT pitch_debug

#define GST_PITCH_GET_PRIVATE(o) (o->priv)
struct _GstPitchPrivate
{
  gfloat stream_time_ratio;

    soundtouch::SoundTouch * st;
};

static GstElementDetails gst_pitch_details =
GST_ELEMENT_DETAILS ("Pitch controller",
    "Filter/Converter/Audio",
    "Control the pitch of an audio stream",
    "Wouter Paesen <wouter@kangaroot.net>");

enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_RATE,
  ARG_TEMPO,
  ARG_PITCH,
};

#define SUPPORTED_CAPS \
GST_STATIC_CAPS( \
  "audio/x-raw-float, " \
    "rate = (int) [ 8000, 48000 ], " \
    "channels = (int) [ 1, 2 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32, " \
    "buffer-frames = (int) [ 0, MAX ]" \
)

static GstStaticPadTemplate gst_pitch_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    SUPPORTED_CAPS);

static GstStaticPadTemplate gst_pitch_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    SUPPORTED_CAPS);

static void gst_pitch_dispose (GObject * object);
static void gst_pitch_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pitch_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static gboolean gst_pitch_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_pitch_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn gst_pitch_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_pitch_sink_event (GstPad * pad, GstEvent * event);
static gboolean gst_pitch_src_event (GstPad * pad, GstEvent * event);

static gboolean gst_pitch_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_pitch_get_query_types (GstPad * pad);

GST_BOILERPLATE (GstPitch, gst_pitch, GstElement, GST_TYPE_ELEMENT);

static void
gst_pitch_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_pitch_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_pitch_sink_template));

  gst_element_class_set_details (gstelement_class, &gst_pitch_details);
}

static void
gst_pitch_class_init (GstPitchClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_pitch_set_property;
  gobject_class->get_property = gst_pitch_get_property;
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_pitch_dispose);
  element_class->change_state = GST_DEBUG_FUNCPTR (gst_pitch_change_state);

  g_object_class_install_property (gobject_class, ARG_PITCH,
      g_param_spec_float ("pitch", "Pitch",
          "Audio stream pitch", 0.1, 10.0, 1.0,
          (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_TEMPO,
      g_param_spec_float ("tempo", "Tempo",
          "Audio stream tempo", 0.1, 10.0, 1.0,
          (GParamFlags) G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, ARG_RATE,
      g_param_spec_float ("rate", "Rate",
          "Audio stream rate", 0.1, 10.0, 1.0,
          (GParamFlags) G_PARAM_READWRITE));

  g_type_class_add_private (gobject_class, sizeof (GstPitchPrivate));
}

static void
gst_pitch_init (GstPitch * pitch, GstPitchClass * pitch_class)
{
  pitch->priv =
      G_TYPE_INSTANCE_GET_PRIVATE ((pitch), GST_TYPE_PITCH, GstPitchPrivate);

  pitch->sinkpad =
      gst_pad_new_from_static_template (&gst_pitch_sink_template, "sink");
  gst_pad_set_chain_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_chain));
  gst_pad_set_event_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_event));
  gst_pad_set_setcaps_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_setcaps));
  gst_pad_set_getcaps_function (pitch->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (pitch), pitch->sinkpad);

  pitch->srcpad =
      gst_pad_new_from_static_template (&gst_pitch_src_template, "src");
  gst_pad_set_event_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_src_event));
  gst_pad_set_query_type_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_get_query_types));
  gst_pad_set_query_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_src_query));
  gst_pad_set_setcaps_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pitch_sink_setcaps));
  gst_pad_set_getcaps_function (pitch->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_element_add_pad (GST_ELEMENT (pitch), pitch->srcpad);

  pitch->priv->st = new soundtouch::SoundTouch ();

  pitch->tempo = 1.0;
  pitch->rate = 1.0;
  pitch->pitch = 1.0;
  pitch->next_buffer_time = 0;
  pitch->next_buffer_offset = 0;

  pitch->priv->st->setRate (pitch->rate);
  pitch->priv->st->setTempo (pitch->tempo);
  pitch->priv->st->setPitch (pitch->pitch);

  pitch->priv->stream_time_ratio = 1.0;
}


static void
gst_pitch_dispose (GObject * object)
{
  GstPitch *pitch = GST_PITCH (object);

  if (pitch->priv->st) {
    delete (pitch->priv->st);
    pitch->priv->st = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_pitch_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPitch *pitch = GST_PITCH (object);

  GST_OBJECT_LOCK (pitch);
  switch (prop_id) {
    case ARG_TEMPO:
      pitch->tempo = g_value_get_float (value);
      pitch->priv->stream_time_ratio = pitch->tempo * pitch->rate;
      pitch->priv->st->setTempo (pitch->tempo);
      break;
    case ARG_RATE:
      pitch->rate = g_value_get_float (value);
      pitch->priv->stream_time_ratio = pitch->tempo * pitch->rate;
      pitch->priv->st->setRate (pitch->rate);
      break;
    case ARG_PITCH:
      pitch->pitch = g_value_get_float (value);
      pitch->priv->st->setPitch (pitch->pitch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (pitch);
}

static void
gst_pitch_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPitch *pitch = GST_PITCH (object);

  GST_OBJECT_LOCK (pitch);
  switch (prop_id) {
    case ARG_TEMPO:
      g_value_set_float (value, pitch->tempo);
      break;
    case ARG_RATE:
      g_value_set_float (value, pitch->rate);
      break;
    case ARG_PITCH:
      g_value_set_float (value, pitch->pitch);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (pitch);
}

static gboolean
gst_pitch_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstPitch *pitch;
  GstPitchPrivate *priv;
  GstStructure *structure;
  GstPad *otherpad;
  gint rate, channels;

  pitch = GST_PITCH (GST_PAD_PARENT (pad));
  priv = GST_PITCH_GET_PRIVATE (pitch);

  otherpad = (pad == pitch->srcpad) ? pitch->sinkpad : pitch->srcpad;

  if (!gst_pad_set_caps (otherpad, caps))
    return FALSE;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate) ||
      !gst_structure_get_int (structure, "channels", &channels)) {
    return FALSE;
  }

  GST_OBJECT_LOCK (pitch);

  pitch->samplerate = rate;
  pitch->channels = channels;

  /* notify the soundtouch instance of this change */
  priv->st->setSampleRate (rate);
  priv->st->setChannels (channels);

  /* calculate sample size */
  pitch->sample_size = (sizeof (gfloat) * channels);
  pitch->sample_duration = gst_util_uint64_scale_int (GST_SECOND, 1, rate);

  GST_OBJECT_UNLOCK (pitch);

  return TRUE;
}

/* send a buffer out */
static GstFlowReturn
gst_pitch_forward_buffer (GstPitch * pitch, GstBuffer * buffer)
{
  gint samples;

  GST_BUFFER_TIMESTAMP (buffer) = pitch->next_buffer_time;
  pitch->next_buffer_time += GST_BUFFER_DURATION (buffer);

  samples = GST_BUFFER_OFFSET (buffer);
  GST_BUFFER_OFFSET (buffer) = pitch->next_buffer_offset;
  pitch->next_buffer_offset += samples;
  GST_BUFFER_OFFSET_END (buffer) = pitch->next_buffer_offset;

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (pitch->srcpad));

  GST_LOG ("pushing buffer [%" GST_TIME_FORMAT "]-[%" GST_TIME_FORMAT
      "] (%d samples)", GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buffer)),
      GST_TIME_ARGS (pitch->next_buffer_time), samples);

  return gst_pad_push (pitch->srcpad, buffer);
}

/* extract a buffer from soundtouch */
static GstBuffer *
gst_pitch_prepare_buffer (GstPitch * pitch)
{
  GstPitchPrivate *priv;
  guint samples;
  GstBuffer *buffer;

  priv = GST_PITCH_GET_PRIVATE (pitch);

  GST_LOG_OBJECT (pitch, "preparing buffer");

  samples = pitch->priv->st->numSamples ();
  if (samples == 0)
    return NULL;;

  buffer = gst_buffer_new_and_alloc (samples * pitch->sample_size);
  samples =
      priv->st->receiveSamples ((gfloat *) GST_BUFFER_DATA (buffer), samples);

  if (samples <= 0)
    return NULL;

  GST_BUFFER_DURATION (buffer) = samples * pitch->sample_duration;
  /* temporary store samples here, to avoid having to recalculate this */
  GST_BUFFER_OFFSET (buffer) = (gint64) samples;

  return buffer;
}

/* process the last samples, in a later stage we should make sure no more
 * samples are sent out here as strictly necessary, because soundtouch could
 * append zero samples, which could disturb looping.  */
static GstFlowReturn
gst_pitch_flush_buffer (GstPitch * pitch)
{
  GstBuffer *buffer;

  GST_DEBUG_OBJECT (pitch, "flushing buffer");

  if (pitch->next_buffer_offset == 0)
    return GST_FLOW_OK;

  pitch->priv->st->flush ();
  buffer = gst_pitch_prepare_buffer (pitch);

  if (!buffer)
    return GST_FLOW_OK;

  return gst_pitch_forward_buffer (pitch, buffer);
}

static gboolean
gst_pitch_src_event (GstPad * pad, GstEvent * event)
{
  GstPitch *pitch;
  gboolean res;

  pitch = GST_PITCH (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      /* transform the event upstream, according to the playback rate */
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gfloat stream_time_ratio;

      GST_OBJECT_LOCK (pitch);
      stream_time_ratio = pitch->priv->stream_time_ratio;
      GST_OBJECT_UNLOCK (pitch);

      gst_event_parse_seek (event, &rate, &format, &flags,
          &cur_type, &cur, &stop_type, &stop);

      cur = (gint64) (cur * stream_time_ratio);
      stop = (gint64) (stop * stream_time_ratio);

      gst_event_unref (event);

      event = gst_event_new_seek (rate, format, flags,
          cur_type, cur, stop_type, stop);

      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (pitch);
  return res;
}

/* generic convert function based on caps, no rate 
 * used here 
 */
static gboolean
gst_pitch_convert (GstPitch * pitch,
    GstFormat src_format, gint64 src_value,
    GstFormat * dst_format, gint64 * dst_value)
{
  gboolean res = TRUE;
  GstClockTime sample_duration;
  guint sample_size;

  g_return_val_if_fail (dst_format && dst_value, FALSE);

  GST_OBJECT_LOCK (pitch);
  sample_duration = pitch->sample_duration;
  sample_size = pitch->sample_size;
  GST_OBJECT_UNLOCK (pitch);

  if (sample_size == 0 || sample_duration == 0 ||
      sample_duration == GST_CLOCK_TIME_NONE) {
    return FALSE;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dst_format) {
        case GST_FORMAT_TIME:
          *dst_value = src_value / sample_size;
          *dst_value *= sample_duration;
          break;
        case GST_FORMAT_DEFAULT:
          *dst_value = src_value / sample_size;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dst_format) {
        case GST_FORMAT_BYTES:
          *dst_value = src_value / sample_duration;
          *dst_value *= sample_size;
          break;
        case GST_FORMAT_DEFAULT:
          *dst_value = src_value / sample_duration;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dst_format) {
        case GST_FORMAT_BYTES:
          *dst_value = src_value * sample_size;
          break;
        case GST_FORMAT_TIME:
          *dst_value = src_value * sample_duration;
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

static const GstQueryType *
gst_pitch_get_query_types (GstPad * pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    GST_QUERY_NONE
  };

  return types;
}

static gboolean
gst_pitch_src_query (GstPad * pad, GstQuery * query)
{
  GstPitch *pitch;
  gboolean res = FALSE;
  gfloat stream_time_ratio;
  gint64 next_buffer_offset;

  pitch = GST_PITCH (gst_pad_get_parent (pad));
  GST_LOG ("%s query", GST_QUERY_TYPE_NAME (query));
  GST_OBJECT_LOCK (pitch);
  stream_time_ratio = pitch->priv->stream_time_ratio;
  next_buffer_offset = pitch->next_buffer_offset;
  GST_OBJECT_UNLOCK (pitch);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_DURATION:{
      GstFormat format;
      gint64 duration;

      if (!gst_pad_query_default (pad, query)) {
        GST_DEBUG_OBJECT (pitch, "upstream provided no duration");
        break;
      }

      gst_query_parse_duration (query, &format, &duration);

      if (format != GST_FORMAT_TIME && format != GST_FORMAT_DEFAULT) {
        GST_DEBUG_OBJECT (pitch, "not TIME or DEFAULT format");
        break;
      }
      GST_LOG_OBJECT (pitch, "upstream duration: %" G_GINT64_FORMAT, duration);
      duration = (gint64) (duration / stream_time_ratio);
      GST_LOG_OBJECT (pitch, "our duration: %" G_GINT64_FORMAT, duration);
      gst_query_set_duration (query, format, duration);
      res = TRUE;
      break;
    }
    case GST_QUERY_POSITION:{
      GstFormat dst_format;
      gint64 dst_value;

      gst_query_parse_position (query, &dst_format, &dst_value);

      if (dst_format != GST_FORMAT_TIME && dst_format != GST_FORMAT_DEFAULT) {
        GST_DEBUG_OBJECT (pitch, "not TIME or DEFAULT format");
        break;
      }

      if (dst_format != GST_FORMAT_DEFAULT) {
        res = gst_pitch_convert (pitch, GST_FORMAT_DEFAULT,
            next_buffer_offset, &dst_format, &dst_value);
      } else {
        dst_value = next_buffer_offset;
        res = TRUE;
      }

      if (res) {
        GST_LOG_OBJECT (pitch, "our position: %" G_GINT64_FORMAT, dst_value);
        gst_query_set_position (query, dst_format, dst_value);
      }
      break;
    }
    case GST_QUERY_CONVERT:{
      GstFormat src_format, dst_format;
      gint64 src_value, dst_value;

      gst_query_parse_convert (query, &src_format, &src_value,
          &dst_format, NULL);

      res = gst_pitch_convert (pitch, src_format, src_value,
          &dst_format, &dst_value);

      if (res) {
        gst_query_set_convert (query, src_format, src_value,
            dst_format, dst_value);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (pitch);
  return res;
}


static gboolean
gst_pitch_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstPitch *pitch;

  pitch = GST_PITCH (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (pad, "received %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
    case GST_EVENT_EOS:
      gst_pitch_flush_buffer (pitch);
      break;
    default:
      break;
  }

  /* and forward it */
  res = gst_pad_event_default (pad, event);

  gst_object_unref (pitch);
  return res;
}

static GstFlowReturn
gst_pitch_chain (GstPad * pad, GstBuffer * buffer)
{
  GstPitch *pitch;
  GstPitchPrivate *priv;

  pitch = GST_PITCH (GST_PAD_PARENT (pad));
  priv = GST_PITCH_GET_PRIVATE (pitch);

  /* push the received samples on the soundtouch buffer */
  GST_LOG_OBJECT (pitch, "incoming buffer (%d samples)",
      (gint) (GST_BUFFER_SIZE (buffer) / pitch->sample_size));

  priv->st->putSamples ((gfloat *) GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer) / pitch->sample_size);
  gst_buffer_unref (buffer);

  /* and try to extract some samples from the soundtouch buffer */
  if (!priv->st->isEmpty ()) {
    GstBuffer *out_buffer;

    out_buffer = gst_pitch_prepare_buffer (pitch);
    return gst_pitch_forward_buffer (pitch, out_buffer);
  }

  return GST_FLOW_OK;
}

static GstStateChangeReturn
gst_pitch_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstPitch *pitch = GST_PITCH (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      pitch->next_buffer_time = 0;
      pitch->next_buffer_offset = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    case GST_STATE_CHANGE_READY_TO_NULL:
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (pitch_debug, "pitch", 0,
      "audio pitch control element");

  return gst_element_register (plugin, "pitch", GST_RANK_NONE, GST_TYPE_PITCH);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "soundtouch",
    "Audio Pitch Controller",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)