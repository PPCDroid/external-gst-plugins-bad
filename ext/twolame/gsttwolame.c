/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2008> Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * Based on the lame element.
 */

/**
 * SECTION:element-twolame
 * @see_also: mad, lame
 *
 * This element encodes raw integer audio into an MPEG-1 layer 2 (MP2) stream.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! twolame ! filesink location=sine.mp2
 * ]| Encode a test sine signal to MP2.
 * |[
 * gst-launch -v alsasrc ! audioconvert ! twolame bitrate=192 ! filesink location=alsasrc.mp2
 * ]| Record from a sound card using ALSA and encode to MP2
 * |[
 * gst-launch -v filesrc location=music.wav ! decodebin ! audioconvert ! audioresample ! twolame bitrate=192 ! id3v2mux ! filesink location=music.mp2
 * ]| Transcode from a .wav file to MP2 (the id3v2mux element is optional)
 * |[
 * gst-launch -v cdda://5 ! audioconvert ! twolame bitrate=192 ! filesink location=track5.mp2
 * ]| Encode Audio CD track 5 to MP2
 * </refsect2>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "string.h"
#include "gsttwolame.h"
#include "gst/gst-i18n-plugin.h"

GST_DEBUG_CATEGORY_STATIC (debug);
#define GST_CAT_DEFAULT debug

/* TwoLAME can do MPEG-1, MPEG-2 so it has 6 possible
 * sample rates it supports */
static GstStaticPadTemplate gst_two_lame_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) { 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate gst_two_lame_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) 2, "
        "rate = (int) { 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

static struct
{
  gint mode;
  gint psymodel;
  gint bitrate;
  gint padding;
  gboolean energy_level_extension;
  gint emphasis;
  gboolean error_protection;
  gboolean copyright;
  gboolean original;
  gboolean vbr;
  gfloat vbr_level;
  gfloat ath_level;
  gint vbr_max_bitrate;
  gboolean quick_mode;
  gint quick_mode_count;
} gst_two_lame_default_settings;

/********** Define useful types for non-programmatic interfaces **********/
#define GST_TYPE_TWO_LAME_MODE (gst_two_lame_mode_get_type())
static GType
gst_two_lame_mode_get_type (void)
{
  static GType two_lame_mode_type = 0;
  static GEnumValue two_lame_modes[] = {
    {TWOLAME_AUTO_MODE, "Auto", "auto"},
    {TWOLAME_STEREO, "Stereo", "stereo"},
    {TWOLAME_JOINT_STEREO, "Joint Stereo", "joint"},
    {TWOLAME_DUAL_CHANNEL, "Dual Channel", "dual"},
    {TWOLAME_MONO, "Mono", "mono"},
    {0, NULL, NULL}
  };

  if (!two_lame_mode_type) {
    two_lame_mode_type =
        g_enum_register_static ("GstTwoLameMode", two_lame_modes);
  }
  return two_lame_mode_type;
}

#define GST_TYPE_TWO_LAME_PADDING (gst_two_lame_padding_get_type())
static GType
gst_two_lame_padding_get_type (void)
{
  static GType two_lame_padding_type = 0;
  static GEnumValue two_lame_padding[] = {
    {TWOLAME_PAD_NO, "No Padding", "never"},
    {TWOLAME_PAD_ALL, "Always Pad", "always"},
    {0, NULL, NULL}
  };

  if (!two_lame_padding_type) {
    two_lame_padding_type =
        g_enum_register_static ("GstTwoLamePadding", two_lame_padding);
  }
  return two_lame_padding_type;
}

#define GST_TYPE_TWO_LAME_EMPHASIS (gst_two_lame_emphasis_get_type())
static GType
gst_two_lame_emphasis_get_type (void)
{
  static GType two_lame_emphasis_type = 0;
  static GEnumValue two_lame_emphasis[] = {
    {TWOLAME_EMPHASIS_N, "No emphasis", "none"},
    {TWOLAME_EMPHASIS_5, "50/15 ms", "5"},
    {TWOLAME_EMPHASIS_C, "CCIT J.17", "ccit"},
    {0, NULL, NULL}
  };

  if (!two_lame_emphasis_type) {
    two_lame_emphasis_type =
        g_enum_register_static ("GstTwoLameEmphasis", two_lame_emphasis);
  }

  return two_lame_emphasis_type;
}

/********** Standard stuff for signals and arguments **********/

enum
{
  ARG_0,
  ARG_MODE,
  ARG_PSYMODEL,
  ARG_BITRATE,
  ARG_PADDING,
  ARG_ENERGY_LEVEL_EXTENSION,
  ARG_EMPHASIS,
  ARG_ERROR_PROTECTION,
  ARG_COPYRIGHT,
  ARG_ORIGINAL,
  ARG_VBR,
  ARG_VBR_LEVEL,
  ARG_ATH_LEVEL,
  ARG_VBR_MAX_BITRATE,
  ARG_QUICK_MODE,
  ARG_QUICK_MODE_COUNT
};

static void gst_two_lame_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_two_lame_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_two_lame_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn gst_two_lame_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_two_lame_setup (GstTwoLame * twolame);
static GstStateChangeReturn gst_two_lame_change_state (GstElement * element,
    GstStateChange transition);


GST_BOILERPLATE (GstTwoLame, gst_two_lame, GstElement, GST_TYPE_ELEMENT);

static void
gst_two_lame_release_memory (GstTwoLame * twolame)
{
  if (twolame->glopts) {
    twolame_close (&twolame->glopts);
    twolame->glopts = NULL;
  }
}

static void
gst_two_lame_finalize (GObject * obj)
{
  gst_two_lame_release_memory (GST_TWO_LAME (obj));

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_two_lame_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_two_lame_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_two_lame_sink_template));
  gst_element_class_set_details_simple (element_class, "TwoLAME mp2 encoder",
      "Codec/Encoder/Audio",
      "High-quality free MP2 encoder",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_two_lame_class_init (GstTwoLameClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_two_lame_set_property;
  gobject_class->get_property = gst_two_lame_get_property;
  gobject_class->finalize = gst_two_lame_finalize;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MODE,
      g_param_spec_enum ("mode", "Mode", "Encoding mode",
          GST_TYPE_TWO_LAME_MODE, gst_two_lame_default_settings.mode,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PSYMODEL,
      g_param_spec_int ("psymodel", "Psychoacoustic Model",
          "Psychoacoustic model used to encode the audio",
          -1, 4, gst_two_lame_default_settings.psymodel,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (kb/s)",
          "Bitrate in kbit/sec (8, 16, 24, 32, 40, 48, 56, 64, 80, 96, "
          "112, 128, 144, 160, 192, 224, 256, 320, 384)",
          8, 384, gst_two_lame_default_settings.bitrate,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PADDING,
      g_param_spec_enum ("padding", "Padding", "Padding type",
          GST_TYPE_TWO_LAME_PADDING, gst_two_lame_default_settings.padding,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_ENERGY_LEVEL_EXTENSION,
      g_param_spec_boolean ("energy-level-extension", "Energy Level Extension",
          "Write peak PCM level to each frame",
          gst_two_lame_default_settings.energy_level_extension,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_EMPHASIS,
      g_param_spec_enum ("emphasis", "Emphasis",
          "Pre-emphasis to apply to the decoded audio",
          GST_TYPE_TWO_LAME_EMPHASIS, gst_two_lame_default_settings.emphasis,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ERROR_PROTECTION,
      g_param_spec_boolean ("error-protection", "Error protection",
          "Adds checksum to every frame",
          gst_two_lame_default_settings.error_protection,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_COPYRIGHT,
      g_param_spec_boolean ("copyright", "Copyright", "Mark as copyright",
          gst_two_lame_default_settings.copyright,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ORIGINAL,
      g_param_spec_boolean ("original", "Original", "Mark as original",
          gst_two_lame_default_settings.original,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR,
      g_param_spec_boolean ("vbr", "VBR", "Enable variable bitrate mode",
          gst_two_lame_default_settings.vbr,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_LEVEL,
      g_param_spec_float ("vbr-level", "VBR Level", "VBR Level",
          -10.0, 10.0, gst_two_lame_default_settings.vbr_level,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ATH_LEVEL,
      g_param_spec_float ("ath-level", "ATH Level", "ATH Level in dB",
          -G_MAXFLOAT, G_MAXFLOAT, gst_two_lame_default_settings.ath_level,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_VBR_MAX_BITRATE,
      g_param_spec_int ("vbr-max-bitrate", "VBR max bitrate",
          "Specify maximum VBR bitrate (0=off, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, "
          "112, 128, 144, 160, 192, 224, 256, 320, 384)",
          0, 384, gst_two_lame_default_settings.vbr_max_bitrate,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUICK_MODE,
      g_param_spec_boolean ("quick-mode", "Quick mode",
          "Calculate Psymodel every frames",
          gst_two_lame_default_settings.quick_mode,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUICK_MODE_COUNT,
      g_param_spec_int ("quick-mode-count", "Quick mode count",
          "Calculate Psymodel every n frames",
          0, G_MAXINT, gst_two_lame_default_settings.quick_mode_count,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_two_lame_change_state);
}

static gboolean
gst_two_lame_src_setcaps (GstPad * pad, GstCaps * caps)
{
  GST_DEBUG_OBJECT (pad, "caps: %" GST_PTR_FORMAT, caps);
  return TRUE;
}

static gboolean
gst_two_lame_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstTwoLame *twolame;
  gint out_samplerate;
  gint version;
  GstStructure *structure;
  GstCaps *othercaps;

  twolame = GST_TWO_LAME (GST_PAD_PARENT (pad));
  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &twolame->samplerate))
    goto no_rate;
  if (!gst_structure_get_int (structure, "channels", &twolame->num_channels))
    goto no_channels;

  GST_DEBUG_OBJECT (twolame, "setting up twolame");
  if (!gst_two_lame_setup (twolame))
    goto setup_failed;

  out_samplerate = twolame_get_out_samplerate (twolame->glopts);
  if (out_samplerate == 0)
    goto zero_output_rate;

  if (out_samplerate != twolame->samplerate) {
    GST_WARNING_OBJECT (twolame,
        "output samplerate %d is different from incoming samplerate %d",
        out_samplerate, twolame->samplerate);
  }

  version = twolame_get_version (twolame->glopts);
  if (version == TWOLAME_MPEG2)
    version = 2;
  else
    version = 1;

  othercaps =
      gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1,
      "mpegaudioversion", G_TYPE_INT, version,
      "layer", G_TYPE_INT, 2,
      "channels", G_TYPE_INT,
      twolame->mode == TWOLAME_MONO ? 1 : twolame->num_channels, "rate",
      G_TYPE_INT, out_samplerate, NULL);

  /* and use these caps */
  gst_pad_set_caps (twolame->srcpad, othercaps);
  gst_caps_unref (othercaps);

  return TRUE;

no_rate:
  {
    GST_ERROR_OBJECT (twolame, "input caps have no sample rate field");
    return FALSE;
  }
no_channels:
  {
    GST_ERROR_OBJECT (twolame, "input caps have no channels field");
    return FALSE;
  }
zero_output_rate:
  {
    GST_ELEMENT_ERROR (twolame, LIBRARY, SETTINGS, (NULL),
        ("TwoLAME decided on a zero sample rate"));
    return FALSE;
  }
setup_failed:
  {
    GST_ELEMENT_ERROR (twolame, LIBRARY, SETTINGS,
        (_("Failed to configure TwoLAME encoder. Check your encoding parameters.")), (NULL));
    return FALSE;
  }
}

static void
gst_two_lame_init (GstTwoLame * twolame, GstTwoLameClass * klass)
{
  GST_DEBUG_OBJECT (twolame, "starting initialization");

  twolame->sinkpad =
      gst_pad_new_from_static_template (&gst_two_lame_sink_template, "sink");
  gst_pad_set_event_function (twolame->sinkpad,
      GST_DEBUG_FUNCPTR (gst_two_lame_sink_event));
  gst_pad_set_chain_function (twolame->sinkpad,
      GST_DEBUG_FUNCPTR (gst_two_lame_chain));
  gst_pad_set_setcaps_function (twolame->sinkpad,
      GST_DEBUG_FUNCPTR (gst_two_lame_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (twolame), twolame->sinkpad);

  twolame->srcpad =
      gst_pad_new_from_static_template (&gst_two_lame_src_template, "src");
  gst_pad_set_setcaps_function (twolame->srcpad,
      GST_DEBUG_FUNCPTR (gst_two_lame_src_setcaps));
  gst_element_add_pad (GST_ELEMENT (twolame), twolame->srcpad);

  twolame->samplerate = 44100;
  twolame->num_channels = 2;
  twolame->setup = FALSE;

  twolame->mode = gst_two_lame_default_settings.mode;
  twolame->psymodel = gst_two_lame_default_settings.psymodel;
  twolame->bitrate = gst_two_lame_default_settings.bitrate;
  twolame->padding = gst_two_lame_default_settings.padding;
  twolame->energy_level_extension =
      gst_two_lame_default_settings.energy_level_extension;
  twolame->emphasis = gst_two_lame_default_settings.emphasis;
  twolame->error_protection = gst_two_lame_default_settings.error_protection;
  twolame->copyright = gst_two_lame_default_settings.copyright;
  twolame->original = gst_two_lame_default_settings.original;
  twolame->vbr = gst_two_lame_default_settings.vbr;
  twolame->vbr_level = gst_two_lame_default_settings.vbr_level;
  twolame->ath_level = gst_two_lame_default_settings.ath_level;
  twolame->vbr_max_bitrate = gst_two_lame_default_settings.vbr_max_bitrate;
  twolame->quick_mode = gst_two_lame_default_settings.quick_mode;
  twolame->quick_mode_count = gst_two_lame_default_settings.quick_mode_count;

  GST_DEBUG_OBJECT (twolame, "done initializing");
}

/* <php-emulation-mode>three underscores for ___rate is really really really
 * private as opposed to one underscore<php-emulation-mode> */
/* call this MACRO outside of the NULL state so that we have a higher chance
 * of actually having a pipeline and bus to get the message through */

#define CHECK_AND_FIXUP_BITRATE(obj,param,rate)         		  \
G_STMT_START {                                                            \
  gint ___rate = rate;                                                    \
  gint maxrate = 320;							  \
  gint multiplier = 64;							  \
  if (rate <= 64) {							  \
    maxrate = 64; multiplier = 8;                                         \
    if ((rate % 8) != 0) ___rate = GST_ROUND_UP_8 (rate); 		  \
  } else if (rate <= 144) {						  \
    maxrate = 144; multiplier = 16;                                       \
    if ((rate % 16) != 0) ___rate = GST_ROUND_UP_16 (rate);               \
  } else if (rate <= 256) {						  \
    maxrate = 256; multiplier = 32;                                       \
    if ((rate % 32) != 0) ___rate = GST_ROUND_UP_32 (rate);               \
  } else if (rate <= 384) { 						  \
    maxrate = 384; multiplier = 64;                                       \
    if ((rate % 64) != 0) ___rate = GST_ROUND_UP_64 (rate);               \
  }                                                                       \
  if (___rate != rate) {                                                  \
    GST_ELEMENT_WARNING (obj, LIBRARY, SETTINGS,			  \
        (_("The requested bitrate %d kbit/s for property '%s' "           \
           "is not allowed. "  					          \
           "The bitrate was changed to %d kbit/s."), rate,		  \
         param,  ___rate), 					          \
        ("A bitrate below %d should be a multiple of %d.", 		  \
            maxrate, multiplier));		  			  \
    rate = ___rate;                                                       \
  }                                                                       \
} G_STMT_END

static void
gst_two_lame_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTwoLame *twolame = GST_TWO_LAME (object);

  switch (prop_id) {
    case ARG_MODE:
      twolame->mode = g_value_get_enum (value);
      break;
    case ARG_PSYMODEL:
      twolame->psymodel = g_value_get_int (value);
      break;
    case ARG_BITRATE:
      twolame->bitrate = g_value_get_int (value);
      break;
    case ARG_PADDING:
      twolame->padding = g_value_get_enum (value);
      break;
    case ARG_ENERGY_LEVEL_EXTENSION:
      twolame->energy_level_extension = g_value_get_boolean (value);
      break;
    case ARG_EMPHASIS:
      twolame->emphasis = g_value_get_enum (value);
      break;
    case ARG_ERROR_PROTECTION:
      twolame->error_protection = g_value_get_boolean (value);
      break;
    case ARG_COPYRIGHT:
      twolame->copyright = g_value_get_boolean (value);
      break;
    case ARG_ORIGINAL:
      twolame->original = g_value_get_boolean (value);
      break;
    case ARG_VBR:
      twolame->vbr = g_value_get_boolean (value);
      break;
    case ARG_VBR_LEVEL:
      twolame->vbr_level = g_value_get_float (value);
      break;
    case ARG_ATH_LEVEL:
      twolame->ath_level = g_value_get_float (value);
      break;
    case ARG_VBR_MAX_BITRATE:
      twolame->vbr_max_bitrate = g_value_get_int (value);
      break;
    case ARG_QUICK_MODE:
      twolame->quick_mode = g_value_get_boolean (value);
      break;
    case ARG_QUICK_MODE_COUNT:
      twolame->quick_mode_count = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_two_lame_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTwoLame *twolame = GST_TWO_LAME (object);

  switch (prop_id) {
    case ARG_MODE:
      g_value_set_enum (value, twolame->mode);
      break;
    case ARG_PSYMODEL:
      g_value_set_int (value, twolame->psymodel);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, twolame->bitrate);
      break;
    case ARG_PADDING:
      g_value_set_enum (value, twolame->padding);
      break;
    case ARG_ENERGY_LEVEL_EXTENSION:
      g_value_set_boolean (value, twolame->energy_level_extension);
      break;
    case ARG_EMPHASIS:
      g_value_set_enum (value, twolame->emphasis);
      break;
    case ARG_ERROR_PROTECTION:
      g_value_set_boolean (value, twolame->error_protection);
      break;
    case ARG_COPYRIGHT:
      g_value_set_boolean (value, twolame->copyright);
      break;
    case ARG_ORIGINAL:
      g_value_set_boolean (value, twolame->original);
      break;
    case ARG_VBR:
      g_value_set_boolean (value, twolame->vbr);
      break;
    case ARG_VBR_LEVEL:
      g_value_set_float (value, twolame->vbr_level);
      break;
    case ARG_ATH_LEVEL:
      g_value_set_float (value, twolame->ath_level);
      break;
    case ARG_VBR_MAX_BITRATE:
      g_value_set_int (value, twolame->vbr_max_bitrate);
      break;
    case ARG_QUICK_MODE:
      g_value_set_boolean (value, twolame->quick_mode);
      break;
    case ARG_QUICK_MODE_COUNT:
      g_value_set_int (value, twolame->quick_mode_count);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_two_lame_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstTwoLame *twolame;

  twolame = GST_TWO_LAME (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:{
      GST_DEBUG_OBJECT (twolame, "handling EOS event");

      if (twolame->glopts != NULL) {
        GstBuffer *buf;
        gint size;

        buf = gst_buffer_new_and_alloc (16384);
        size =
            twolame_encode_flush (twolame->glopts, GST_BUFFER_DATA (buf),
            16394);

        if (size > 0 && twolame->last_flow == GST_FLOW_OK) {
          gint64 duration;

          duration = gst_util_uint64_scale (size, 8 * GST_SECOND,
              1000 * twolame->bitrate);

          if (twolame->last_ts == GST_CLOCK_TIME_NONE) {
            twolame->last_ts = twolame->eos_ts;
            twolame->last_duration = duration;
          } else {
            twolame->last_duration += duration;
          }

          GST_BUFFER_TIMESTAMP (buf) = twolame->last_ts;
          GST_BUFFER_DURATION (buf) = twolame->last_duration;
          twolame->last_ts = GST_CLOCK_TIME_NONE;
          GST_BUFFER_SIZE (buf) = size;
          GST_DEBUG_OBJECT (twolame, "pushing final packet of %u bytes", size);
          gst_buffer_set_caps (buf, GST_PAD_CAPS (twolame->srcpad));
          gst_pad_push (twolame->srcpad, buf);
        } else {
          GST_DEBUG_OBJECT (twolame, "no final packet (size=%d, last_flow=%s)",
              size, gst_flow_get_name (twolame->last_flow));
          gst_buffer_unref (buf);
        }
      }

      ret = gst_pad_event_default (pad, event);
      break;
    }
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (twolame, "handling FLUSH start event");
      /* forward event */
      ret = gst_pad_push_event (twolame->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
    {
      guchar *mp3_data = NULL;
      gint mp3_buffer_size, mp3_size = 0;

      GST_DEBUG_OBJECT (twolame, "handling FLUSH stop event");

      /* clear buffers */
      mp3_buffer_size = 16384;
      mp3_data = g_malloc (mp3_buffer_size);
      mp3_size =
          twolame_encode_flush (twolame->glopts, mp3_data, mp3_buffer_size);

      ret = gst_pad_push_event (twolame->srcpad, event);

      g_free (mp3_data);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }
  gst_object_unref (twolame);

  return ret;
}

static GstFlowReturn
gst_two_lame_chain (GstPad * pad, GstBuffer * buf)
{
  GstTwoLame *twolame;
  guchar *mp3_data;
  gint mp3_buffer_size, mp3_size;
  gint64 duration;
  GstFlowReturn result;
  gint num_samples;
  guint8 *data;
  guint size;

  twolame = GST_TWO_LAME (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (twolame, "entered chain");

  if (!twolame->setup)
    goto not_setup;

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  num_samples = size / 2;

  /* allocate space for output */
  mp3_buffer_size = 1.25 * num_samples + 16384;
  mp3_data = g_malloc (mp3_buffer_size);

  if (twolame->num_channels == 1) {
    mp3_size = twolame_encode_buffer (twolame->glopts,
        (short int *) data,
        (short int *) data, num_samples, mp3_data, mp3_buffer_size);
  } else {
    mp3_size = twolame_encode_buffer_interleaved (twolame->glopts,
        (short int *) data,
        num_samples / twolame->num_channels, mp3_data, mp3_buffer_size);
  }

  GST_LOG_OBJECT (twolame, "encoded %d bytes of audio to %d bytes of mp3",
      size, mp3_size);

  duration = gst_util_uint64_scale_int (size, GST_SECOND,
      2 * twolame->samplerate * twolame->num_channels);

  if (GST_BUFFER_DURATION (buf) != GST_CLOCK_TIME_NONE &&
      GST_BUFFER_DURATION (buf) != duration) {
    GST_DEBUG_OBJECT (twolame, "incoming buffer had incorrect duration %"
        GST_TIME_FORMAT ", outgoing buffer will have correct duration %"
        GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)), GST_TIME_ARGS (duration));
  }

  if (twolame->last_ts == GST_CLOCK_TIME_NONE) {
    twolame->last_ts = GST_BUFFER_TIMESTAMP (buf);
    twolame->last_offs = GST_BUFFER_OFFSET (buf);
    twolame->last_duration = duration;
  } else {
    twolame->last_duration += duration;
  }

  gst_buffer_unref (buf);

  if (mp3_size < 0) {
    g_warning ("error %d", mp3_size);
  }

  if (mp3_size > 0) {
    GstBuffer *outbuf;

    outbuf = gst_buffer_new ();
    GST_BUFFER_DATA (outbuf) = mp3_data;
    GST_BUFFER_MALLOCDATA (outbuf) = mp3_data;
    GST_BUFFER_SIZE (outbuf) = mp3_size;
    GST_BUFFER_TIMESTAMP (outbuf) = twolame->last_ts;
    GST_BUFFER_OFFSET (outbuf) = twolame->last_offs;
    GST_BUFFER_DURATION (outbuf) = twolame->last_duration;
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (twolame->srcpad));

    result = gst_pad_push (twolame->srcpad, outbuf);
    twolame->last_flow = result;
    if (result != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (twolame, "flow return: %s", gst_flow_get_name (result));
    }

    if (GST_CLOCK_TIME_IS_VALID (twolame->last_ts))
      twolame->eos_ts = twolame->last_ts + twolame->last_duration;
    else
      twolame->eos_ts = GST_CLOCK_TIME_NONE;
    twolame->last_ts = GST_CLOCK_TIME_NONE;
  } else {
    g_free (mp3_data);
    result = GST_FLOW_OK;
  }

  return result;

  /* ERRORS */
not_setup:
  {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (twolame, CORE, NEGOTIATION, (NULL),
        ("encoder not initialized (input is not audio?)"));
    return GST_FLOW_ERROR;
  }
}

/* set up the encoder state */
static gboolean
gst_two_lame_setup (GstTwoLame * twolame)
{

#define CHECK_ERROR(command) G_STMT_START {\
  if ((command) < 0) { \
    GST_ERROR_OBJECT (twolame, "setup failed: " G_STRINGIFY (command)); \
    return FALSE; \
  } \
}G_STMT_END

  int retval;
  GstCaps *allowed_caps;

  GST_DEBUG_OBJECT (twolame, "starting setup");

  /* check if we're already setup; if we are, we might want to check
   * if this initialization is compatible with the previous one */
  /* FIXME: do this */
  if (twolame->setup) {
    GST_WARNING_OBJECT (twolame, "already setup");
    twolame->setup = FALSE;
  }

  twolame->glopts = twolame_init ();

  if (twolame->glopts == NULL)
    return FALSE;

  /* copy the parameters over */
  twolame_set_in_samplerate (twolame->glopts, twolame->samplerate);

  /* let twolame choose default samplerate unless outgoing sample rate is fixed */
  allowed_caps = gst_pad_get_allowed_caps (twolame->srcpad);

  if (allowed_caps != NULL) {
    GstStructure *structure;
    gint samplerate;

    structure = gst_caps_get_structure (allowed_caps, 0);

    if (gst_structure_get_int (structure, "rate", &samplerate)) {
      GST_DEBUG_OBJECT (twolame,
          "Setting sample rate to %d as fixed in src caps", samplerate);
      twolame_set_out_samplerate (twolame->glopts, samplerate);
    } else {
      GST_DEBUG_OBJECT (twolame, "Letting twolame choose sample rate");
      twolame_set_out_samplerate (twolame->glopts, 0);
    }
    gst_caps_unref (allowed_caps);
    allowed_caps = NULL;
  } else {
    GST_DEBUG_OBJECT (twolame,
        "No peer yet, letting twolame choose sample rate");
    twolame_set_out_samplerate (twolame->glopts, 0);
  }

  /* force mono encoding if we only have one channel */
  if (twolame->num_channels == 1)
    twolame->mode = 3;

  /* Fix bitrates and MPEG version */

  CHECK_ERROR (twolame_set_num_channels (twolame->glopts,
          twolame->num_channels));

  CHECK_ERROR (twolame_set_mode (twolame->glopts, twolame->mode));
  CHECK_ERROR (twolame_set_psymodel (twolame->glopts, twolame->psymodel));
  CHECK_AND_FIXUP_BITRATE (twolame, "bitrate", twolame->bitrate);
  CHECK_ERROR (twolame_set_bitrate (twolame->glopts, twolame->bitrate));
  CHECK_ERROR (twolame_set_padding (twolame->glopts, twolame->padding));
  CHECK_ERROR (twolame_set_energy_levels (twolame->glopts,
          twolame->energy_level_extension));
  CHECK_ERROR (twolame_set_emphasis (twolame->glopts, twolame->emphasis));
  CHECK_ERROR (twolame_set_error_protection (twolame->glopts,
          twolame->error_protection));
  CHECK_ERROR (twolame_set_copyright (twolame->glopts, twolame->copyright));
  CHECK_ERROR (twolame_set_original (twolame->glopts, twolame->original));
  CHECK_ERROR (twolame_set_VBR (twolame->glopts, twolame->vbr));
  CHECK_ERROR (twolame_set_VBR_level (twolame->glopts, twolame->vbr_level));
  CHECK_ERROR (twolame_set_ATH_level (twolame->glopts, twolame->ath_level));
  CHECK_AND_FIXUP_BITRATE (twolame, "vbr-max-bitrate",
      twolame->vbr_max_bitrate);
  CHECK_ERROR (twolame_set_VBR_max_bitrate_kbps (twolame->glopts,
          twolame->vbr_max_bitrate));
  CHECK_ERROR (twolame_set_quick_mode (twolame->glopts, twolame->quick_mode));
  CHECK_ERROR (twolame_set_quick_count (twolame->glopts,
          twolame->quick_mode_count));

  /* initialize the twolame encoder */
  if ((retval = twolame_init_params (twolame->glopts)) >= 0) {
    twolame->setup = TRUE;
    /* FIXME: it would be nice to print out the mode here */
    GST_INFO ("twolame encoder setup (%d kbit/s, %d Hz, %d channels)",
        twolame->bitrate, twolame->samplerate, twolame->num_channels);
  } else {
    GST_ERROR_OBJECT (twolame, "twolame_init_params returned %d", retval);
  }

  GST_DEBUG_OBJECT (twolame, "done with setup");

  return twolame->setup;
#undef CHECK_ERROR
}

static GstStateChangeReturn
gst_two_lame_change_state (GstElement * element, GstStateChange transition)
{
  GstTwoLame *twolame;
  GstStateChangeReturn result;

  twolame = GST_TWO_LAME (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      twolame->last_flow = GST_FLOW_OK;
      twolame->last_ts = GST_CLOCK_TIME_NONE;
      twolame->eos_ts = GST_CLOCK_TIME_NONE;
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_two_lame_release_memory (twolame);
      break;
    default:
      break;
  }

  return result;
}

static gboolean
gst_two_lame_get_default_settings (void)
{
  twolame_options *glopts = NULL;

  glopts = twolame_init ();
  if (glopts == NULL) {
    GST_ERROR ("Couldn't initialize TwoLAME");
    return FALSE;
  }

  twolame_set_num_channels (glopts, 2);
  twolame_set_in_samplerate (glopts, 44100);

  if (twolame_init_params (glopts) != 0) {
    GST_ERROR ("Couldn't set default parameters");
    return FALSE;
  }

  gst_two_lame_default_settings.mode = TWOLAME_JOINT_STEREO;    /* twolame_get_mode (glopts); */
  gst_two_lame_default_settings.psymodel = twolame_get_psymodel (glopts);
  gst_two_lame_default_settings.bitrate = twolame_get_bitrate (glopts);
  gst_two_lame_default_settings.padding = twolame_get_padding (glopts);
  gst_two_lame_default_settings.energy_level_extension =
      twolame_get_energy_levels (glopts);
  gst_two_lame_default_settings.emphasis = twolame_get_emphasis (glopts);
  gst_two_lame_default_settings.error_protection =
      twolame_get_error_protection (glopts);
  gst_two_lame_default_settings.copyright = twolame_get_copyright (glopts);
  gst_two_lame_default_settings.original = twolame_get_original (glopts);
  gst_two_lame_default_settings.vbr = twolame_get_VBR (glopts);
  gst_two_lame_default_settings.vbr_level = twolame_get_VBR_level (glopts);
  gst_two_lame_default_settings.ath_level = twolame_get_ATH_level (glopts);
  gst_two_lame_default_settings.vbr_max_bitrate =
      twolame_get_VBR_max_bitrate_kbps (glopts);
  gst_two_lame_default_settings.quick_mode = twolame_get_quick_mode (glopts);
  gst_two_lame_default_settings.quick_mode_count =
      twolame_get_quick_count (glopts);

  twolame_close (&glopts);

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (debug, "twolame", 0, "twolame mp2 encoder");

  if (!gst_two_lame_get_default_settings ())
    return FALSE;

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
#endif /* ENABLE_NLS */

  if (!gst_element_register (plugin, "twolame", GST_RANK_NONE,
          GST_TYPE_TWO_LAME))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "twolame",
    "Encode MP2s with TwoLAME",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);