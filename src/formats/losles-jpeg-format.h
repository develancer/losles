#pragma once

#include "losles-format.h"

G_BEGIN_DECLS

#define LOSLES_TYPE_JPEG_FORMAT (losles_jpeg_format_get_type())
G_DECLARE_FINAL_TYPE(LoslesJpegFormat,
                     losles_jpeg_format,
                     LOSLES,
                     JPEG_FORMAT,
                     GObject)

GObject *losles_jpeg_format_new(void);

G_END_DECLS
