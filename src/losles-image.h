#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

typedef enum {
  LOSLES_PIXEL_FORMAT_G8,
  LOSLES_PIXEL_FORMAT_GA8,
  LOSLES_PIXEL_FORMAT_RGB8,
  LOSLES_PIXEL_FORMAT_RGBA8,
} LoslesPixelFormat;

#define LOSLES_TYPE_IMAGE (losles_image_get_type())
G_DECLARE_FINAL_TYPE(LoslesImage, losles_image, LOSLES, IMAGE, GObject)

LoslesImage *losles_image_new(GFile *file,
                              guint width,
                              guint height,
                              guint stride,
                              LoslesPixelFormat pixel_format,
                              GBytes *pixels,
                              GBytes *icc_profile,
                              guint orientation,
                              gboolean has_exif_orientation,
                              guint jpeg_mcu_width,
                              guint jpeg_mcu_height,
                              const gchar *format_name,
                              GObject *format);

GFile *losles_image_get_file(LoslesImage *self);
guint losles_image_get_width(LoslesImage *self);
guint losles_image_get_height(LoslesImage *self);
guint losles_image_get_display_width(LoslesImage *self);
guint losles_image_get_display_height(LoslesImage *self);
guint losles_image_get_stride(LoslesImage *self);
LoslesPixelFormat losles_image_get_pixel_format(LoslesImage *self);
GBytes *losles_image_get_pixels(LoslesImage *self);
GBytes *losles_image_get_icc_profile(LoslesImage *self);
guint losles_image_get_orientation(LoslesImage *self);
gboolean losles_image_has_exif_orientation(LoslesImage *self);
guint losles_image_get_jpeg_mcu_width(LoslesImage *self);
guint losles_image_get_jpeg_mcu_height(LoslesImage *self);
const gchar *losles_image_get_format_name(LoslesImage *self);
GObject *losles_image_get_format(LoslesImage *self);
gsize losles_image_get_memory_size(LoslesImage *self);

G_END_DECLS
