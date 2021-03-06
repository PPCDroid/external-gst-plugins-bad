/* GStreamer Jack plugins
 * Copyright (C) 2006 Wim Taymans <wim@fluendo.com>
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

#include "gstjackaudiosrc.h"
#include "gstjackaudiosink.h"

GType
gst_jack_connect_get_type (void)
{
  static GType jack_connect_type = 0;
  static const GEnumValue jack_connect[] = {
    {GST_JACK_CONNECT_NONE,
        "Don't automatically connect ports to physical ports", "none"},
    {GST_JACK_CONNECT_AUTO,
        "Automatically connect ports to physical ports", "auto"},
    {0, NULL, NULL},
  };

  if (!jack_connect_type) {
    jack_connect_type = g_enum_register_static ("GstJackConnect", jack_connect);
  }
  return jack_connect_type;
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "jackaudiosrc", GST_RANK_PRIMARY,
          GST_TYPE_JACK_AUDIO_SRC))
    return FALSE;
  if (!gst_element_register (plugin, "jackaudiosink", GST_RANK_PRIMARY,
          GST_TYPE_JACK_AUDIO_SINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "jack",
    "Jack elements",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
