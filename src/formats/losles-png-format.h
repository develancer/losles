#pragma once

#include "losles-format.h"

G_BEGIN_DECLS

#define LOSLES_TYPE_PNG_FORMAT (losles_png_format_get_type())
G_DECLARE_FINAL_TYPE(LoslesPngFormat,
                     losles_png_format,
                     LOSLES,
                     PNG_FORMAT,
                     GObject)

GObject *losles_png_format_new(void);

G_END_DECLS
