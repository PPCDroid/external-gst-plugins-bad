/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *		 <2003> David Schleef <ds@schleef.org>
 *		 <2004> Benjamin Otte <otte@gnome.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gstvideofilter.h>
#include "gstvideoimage.h"
#include <string.h>

#define GST_TYPE_PUZZLE \
  (gst_puzzle_get_type())
#define GST_PUZZLE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PUZZLE,GstPuzzle))
#define GST_PUZZLE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PUZZLE,GstPuzzleClass))
#define GST_IS_PUZZLE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PUZZLE))
#define GST_IS_PUZZLE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PUZZLE))

typedef struct _GstPuzzle GstPuzzle;
typedef struct _GstPuzzleClass GstPuzzleClass;

struct _GstPuzzle
{
  GstVideofilter videofilter;

  const GstVideoFormat *format;
  /* properties */
  guint rows;
  guint columns;
  guint tiles;
  /* state */
  guint *permutation;
  guint position;
  gboolean solved;
};

struct _GstPuzzleClass
{
  GstVideofilterClass parent_class;
};


/* GstPuzzle signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_COLUMNS,
  ARG_ROWS
      /* FILL ME */
};

static void gst_puzzle_base_init (gpointer g_class);
static void gst_puzzle_class_init (gpointer g_class, gpointer class_data);
static void gst_puzzle_init (GTypeInstance * instance, gpointer g_class);

static void gst_puzzle_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_puzzle_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_puzzle_setup (GstVideofilter * videofilter);
static void draw_puzzle (GstVideofilter * videofilter, void *destp, void *srcp);

GType
gst_puzzle_get_type (void)
{
  static GType puzzle_type = 0;

  if (!puzzle_type) {
    static const GTypeInfo puzzle_info = {
      sizeof (GstPuzzleClass),
      gst_puzzle_base_init,
      NULL,
      gst_puzzle_class_init,
      NULL,
      NULL,
      sizeof (GstPuzzle),
      0,
      gst_puzzle_init,
    };

    puzzle_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
        "GstPuzzle", &puzzle_info, 0);
  }
  return puzzle_type;
}

static void
gst_puzzle_base_init (gpointer g_class)
{
  static GstElementDetails puzzle_details =
      GST_ELEMENT_DETAILS ("a simple puzzle",
      "Filter/Effect/Video/Games",
      "A simple puzzle, use arrow keys and space to restart/solve",
      "Benjamin Otte <otte@gnome.org>");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstVideofilterClass *videofilter_class = GST_VIDEOFILTER_CLASS (g_class);
  int i;
  GstVideofilterFormat *f;

  gst_element_class_set_details (element_class, &puzzle_details);

  for (i = 0; i < gst_video_format_count; i++) {
    f = g_new0 (GstVideofilterFormat, 1);
    f->fourcc = gst_video_format_list[i].fourcc;
    f->bpp = gst_video_format_list[i].bitspp;
    f->filter_func = draw_puzzle;
    if (gst_video_format_list[i].ext_caps) {
      f->depth = gst_video_format_list[i].depth;
      f->endianness = G_BYTE_ORDER;
      f->red_mask = gst_video_format_list[i].red_mask;
      f->green_mask = gst_video_format_list[i].green_mask;
      f->blue_mask = gst_video_format_list[i].blue_mask;
    }
    gst_videofilter_class_add_format (videofilter_class, f);
  }

  gst_videofilter_class_add_pad_templates (GST_VIDEOFILTER_CLASS (g_class));
}

static void
gst_puzzle_class_init (gpointer g_class, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstVideofilterClass *videofilter_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  videofilter_class = GST_VIDEOFILTER_CLASS (g_class);

  gobject_class->set_property = gst_puzzle_set_property;
  gobject_class->get_property = gst_puzzle_get_property;

  g_object_class_install_property (gobject_class, ARG_ROWS,
      g_param_spec_uint ("rows", "rows", "number of rows in puzzle",
          1, G_MAXUINT, 4, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, ARG_COLUMNS,
      g_param_spec_uint ("columns", "columns", "number of columns in puzzle",
          1, G_MAXUINT, 4, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  videofilter_class->setup = gst_puzzle_setup;
}

static void G_GNUC_UNUSED
gst_puzzle_solve (GstPuzzle * puzzle)
{
  guint i;

  for (i = 0; i < puzzle->tiles; i++) {
    puzzle->permutation[i] = i;
  }
  puzzle->position = puzzle->tiles - 1;
  puzzle->solved = TRUE;
}

static gboolean
gst_puzzle_is_solved (GstPuzzle * puzzle)
{
  guint i;

  if (puzzle->position != puzzle->tiles - 1)
    return FALSE;

  for (i = 0; i < puzzle->tiles; i++) {
    if (puzzle->permutation[i] != i)
      return FALSE;
  }

  return TRUE;
}

#if 0
static void
gst_puzzle_show (GstPuzzle * puzzle)
{
  guint i;

  for (i = 0; i < puzzle->tiles; i++) {
    g_print ("%d ", puzzle->permutation[i]);
  }
  g_print ("\n");
}
#endif

typedef enum
{
  DIR_UP,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT
} GstPuzzleDirection;

static void
gst_puzzle_move (GstPuzzle * puzzle, GstPuzzleDirection dir)
{
  guint next = puzzle->tiles;

  switch (dir) {
    case DIR_UP:
      if (puzzle->position >= puzzle->columns)
        next = puzzle->position - puzzle->columns;
      break;
    case DIR_DOWN:
      if (puzzle->tiles - puzzle->position > puzzle->columns)
        next = puzzle->position + puzzle->columns;
      break;
    case DIR_LEFT:
      if ((puzzle->position % puzzle->columns) > 0)
        next = puzzle->position - 1;
      break;
    case DIR_RIGHT:
      if ((puzzle->position % puzzle->columns) < puzzle->columns - 1)
        next = puzzle->position + 1;
      break;
    default:
      g_assert_not_reached ();
  }

  if (next < puzzle->tiles) {
    /* the move was valid */
    guint tmp = puzzle->permutation[puzzle->position];

    puzzle->permutation[puzzle->position] = puzzle->permutation[next];
    puzzle->permutation[next] = tmp;
    puzzle->position = next;
    puzzle->solved = gst_puzzle_is_solved (puzzle);
  }
}

static void
gst_puzzle_shuffle (GstPuzzle * puzzle)
{
  guint i;

  do {
    for (i = 0; i < 100 * puzzle->tiles; i++) {
      gst_puzzle_move (puzzle, g_random_int_range (0, 4));
    }
  } while (gst_puzzle_is_solved (puzzle));
  puzzle->solved = FALSE;
}

static gboolean
nav_event_handler (GstPad * pad, GstEvent * event)
{
  GstPuzzle *puzzle;
  const gchar *type;

  puzzle = GST_PUZZLE (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      type = gst_structure_get_string (event->event_data.structure.structure,
          "event");
      if (g_str_equal (type, "key-press")) {
        const gchar *key =
            gst_structure_get_string (event->event_data.structure.structure,
            "key");
        if (g_str_equal (key, "space")) {
          if (gst_puzzle_is_solved (puzzle)) {
            gst_puzzle_shuffle (puzzle);
          } else {
            gst_puzzle_solve (puzzle);
          }
        } else {
          if (puzzle->solved)
            break;
          if (g_str_equal (key, "Left")) {
            gst_puzzle_move (puzzle, DIR_LEFT);
          } else if (g_str_equal (key, "Right")) {
            gst_puzzle_move (puzzle, DIR_RIGHT);
          } else if (g_str_equal (key, "Up")) {
            gst_puzzle_move (puzzle, DIR_UP);
          } else if (g_str_equal (key, "Down")) {
            gst_puzzle_move (puzzle, DIR_DOWN);
          } else {
            break;
          }
        }
        gst_event_unref (event);
        return TRUE;
      }
      break;
    default:
      break;
  }
  return gst_pad_event_default (pad, event);
}

static void
gst_puzzle_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideofilter *videofilter;

  videofilter = GST_VIDEOFILTER (instance);
  /* FIXME: this is evil */
  gst_pad_set_event_function (videofilter->srcpad, nav_event_handler);
}

static void
gst_puzzle_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPuzzle *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PUZZLE (object));
  src = GST_PUZZLE (object);

  GST_DEBUG ("gst_puzzle_set_property");
  switch (prop_id) {
    case ARG_COLUMNS:
      src->columns = g_value_get_uint (value);
      src->tiles = src->columns * src->rows;
      break;
    case ARG_ROWS:
      src->rows = g_value_get_uint (value);
      src->tiles = src->columns * src->rows;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_puzzle_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPuzzle *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_PUZZLE (object));
  src = GST_PUZZLE (object);

  switch (prop_id) {
    case ARG_COLUMNS:
      g_value_set_uint (value, src->columns);
      break;
    case ARG_ROWS:
      g_value_set_uint (value, src->rows);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_puzzle_setup (GstVideofilter * videofilter)
{
  GstPuzzle *puzzle;
  guint i;

  g_return_if_fail (GST_IS_PUZZLE (videofilter));
  puzzle = GST_PUZZLE (videofilter);

  g_free (puzzle->permutation);
  puzzle->permutation = g_new (guint, puzzle->tiles);
  for (i = 0; i < puzzle->tiles; i++) {
    puzzle->permutation[i] = i;
  }
  puzzle->position = puzzle->tiles - 1;
  /* shuffle a bit */
  gst_puzzle_shuffle (puzzle);
  puzzle->format = NULL;
}

static void
draw_puzzle (GstVideofilter * videofilter, void *destp, void *srcp)
{
  GstPuzzle *puzzle;
  int width, height;
  guint i;
  GstVideoImage dest, src;

  puzzle = GST_PUZZLE (videofilter);
  if (!puzzle->format) {
    puzzle->format =
        gst_video_format_find_by_structure (gst_caps_get_structure
        (gst_pad_get_negotiated_caps (videofilter->sinkpad), 0));
  }
  width = gst_videofilter_get_input_width (videofilter);
  height = gst_videofilter_get_input_height (videofilter);
  gst_video_image_setup (&dest, puzzle->format, destp, width, height);
  gst_video_image_setup (&src, puzzle->format, srcp, width, height);
  if (width % puzzle->columns != 0) {
    guint w = width % puzzle->columns;

    gst_video_image_copy_area (&dest, width - w, 0, &src, width - w, 0, w,
        height);
  }
  if (height % puzzle->rows != 0) {
    guint h = height % puzzle->rows;

    gst_video_image_copy_area (&dest, 0, height - h, &src, 0, height - h, width,
        h);
  }
  width /= puzzle->columns;
  height /= puzzle->rows;

  for (i = 0; i < puzzle->tiles; i++) {
    if (!puzzle->solved && i == puzzle->position) {
      gst_video_image_draw_rectangle (&dest, width * (i % puzzle->columns),
          height * (i / puzzle->columns), width, height,
          &GST_VIDEO_COLOR_WHITE, TRUE);
    } else {
      gst_video_image_copy_area (&dest, width * (i % puzzle->columns),
          height * (i / puzzle->columns), &src,
          width * (puzzle->permutation[i] % puzzle->columns),
          height * (puzzle->permutation[i] / puzzle->columns), width, height);
    }
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstvideofilter"))
    return FALSE;

  return gst_element_register (plugin, "puzzle", GST_RANK_NONE,
      GST_TYPE_PUZZLE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "games",
    "a collection of games to showcase features",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)