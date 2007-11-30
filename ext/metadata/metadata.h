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

#ifndef __METADATA_H__
#define __METADATA_H__

#include <gst/base/gstadapter.h>
#include "metadatatypes.h"

#include "metadataparsejpeg.h"
#include "metadatamuxjpeg.h"
#include "metadataparsepng.h"
#include "metadatamuxpng.h"

G_BEGIN_DECLS

typedef enum _tag_MetaOption
{
  META_OPT_EXIF = (1 << 0),
  META_OPT_IPTC = (1 << 1),
  META_OPT_XMP = (1 << 2),
  META_OPT_ALL = (1 << 3) - 1
} MetaOption;

typedef enum _tag_MetaState
{
  STATE_NULL,
  STATE_READING,
  STATE_DONE
} MetaState;

typedef enum _tag_ImageType
{
  IMG_NONE,
  IMG_JPEG,
  IMG_PNG
} ImageType;

typedef struct _tag_MetaData
{
  MetaState state;
  ImageType img_type;
  MetaOption option;
  guint32 offset_orig; /* offset since begining of stream */
  union
  {
    JpegParseData jpeg_parse;
    JpegMuxData jpeg_mux;
    PngParseData png_parse;
    PngMuxData png_mux;
  } format_data;
  GstAdapter * exif_adapter;
  GstAdapter * iptc_adapter;
  GstAdapter * xmp_adapter;

  MetadataChunkArray strip_chunks;
  MetadataChunkArray inject_chunks;

  gboolean parse; /* true - parsing, false - muxing */

} MetaData;

#define META_DATA_IMG_TYPE(p) (p).img_type
#define META_DATA_OPTION(p) (p).option
#define set_meta_option(p, m) do { (p).option = (p).option | (m); } while(FALSE)
#define unset_meta_option(p, m) do { (p).option = (p).option & ~(m); } while(FALSE)

extern void metadata_init (MetaData * meta_data, gboolean parse);

/*
 * offset: number of bytes that MUST be jumped after current "buf" pointer
 * next_size: number of minimum amount of bytes required on next step.
 *            if less than this is provided, the return will be 1 for sure.
 *            and the offset will be 0 (zero)
 * return:
 *   -1 -> error
 *    0 -> done
 *    1 -> need more data
 */
extern int
metadata_parse (MetaData * meta_data, const guint8 * buf,
    guint32 bufsize, guint32 * next_offset, guint32 * next_size);


extern void metadata_lazy_update (MetaData * meta_data);

extern void metadata_dispose (MetaData * meta_data);

G_END_DECLS
#endif /* __METADATA_H__ */