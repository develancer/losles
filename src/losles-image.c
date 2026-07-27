#include "losles-image.h"

struct _LoslesImage {
  GObject parent_instance;

  GFile *file;
  guint width;
  guint height;
  guint stride;
  LoslesPixelFormat pixel_format;
  GBytes *pixels;
  GBytes *icc_profile;
  guint orientation;
  guint jpeg_mcu_width;
  guint jpeg_mcu_height;
  gchar *format_name;
  GObject *format;
};

G_DEFINE_FINAL_TYPE(LoslesImage, losles_image, G_TYPE_OBJECT)

static void
losles_image_finalize(GObject *object)
{
  LoslesImage *self = LOSLES_IMAGE(object);

  g_clear_object(&self->file);
  g_clear_pointer(&self->pixels, g_bytes_unref);
  g_clear_pointer(&self->icc_profile, g_bytes_unref);
  g_clear_pointer(&self->format_name, g_free);
  g_clear_object(&self->format);

  G_OBJECT_CLASS(losles_image_parent_class)->finalize(object);
}

static void
losles_image_class_init(LoslesImageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = losles_image_finalize;
}

static void
losles_image_init(LoslesImage *self)
{
  self->orientation = 1;
}

LoslesImage *
losles_image_new(GFile *file,
                 guint width,
                 guint height,
                 guint stride,
                 LoslesPixelFormat pixel_format,
                 GBytes *pixels,
                 GBytes *icc_profile,
                 guint orientation,
                 guint jpeg_mcu_width,
                 guint jpeg_mcu_height,
                 const gchar *format_name,
                 GObject *format)
{
  g_return_val_if_fail(G_IS_FILE(file), NULL);
  g_return_val_if_fail(pixels != NULL, NULL);
  g_return_val_if_fail(width > 0 && height > 0, NULL);

  LoslesImage *self = g_object_new(LOSLES_TYPE_IMAGE, NULL);
  self->file = g_object_ref(file);
  self->width = width;
  self->height = height;
  self->stride = stride;
  self->pixel_format = pixel_format;
  self->pixels = g_bytes_ref(pixels);
  self->icc_profile = icc_profile ? g_bytes_ref(icc_profile) : NULL;
  self->orientation = orientation >= 1 && orientation <= 8 ? orientation : 1;
  self->jpeg_mcu_width = jpeg_mcu_width;
  self->jpeg_mcu_height = jpeg_mcu_height;
  self->format_name = g_strdup(format_name);
  self->format = format ? g_object_ref(format) : NULL;

  return self;
}

GFile *
losles_image_get_file(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), NULL);
  return self->file;
}

guint
losles_image_get_width(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->width;
}

guint
losles_image_get_height(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->height;
}

guint
losles_image_get_display_width(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->orientation >= 5 && self->orientation <= 8
           ? self->height
           : self->width;
}

guint
losles_image_get_display_height(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->orientation >= 5 && self->orientation <= 8
           ? self->width
           : self->height;
}

guint
losles_image_get_stride(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->stride;
}

LoslesPixelFormat
losles_image_get_pixel_format(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), LOSLES_PIXEL_FORMAT_RGB8);
  return self->pixel_format;
}

GBytes *
losles_image_get_pixels(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), NULL);
  return self->pixels;
}

GBytes *
losles_image_get_icc_profile(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), NULL);
  return self->icc_profile;
}

guint
losles_image_get_orientation(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 1);
  return self->orientation;
}

guint
losles_image_get_jpeg_mcu_width(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->jpeg_mcu_width;
}

guint
losles_image_get_jpeg_mcu_height(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);
  return self->jpeg_mcu_height;
}

const gchar *
losles_image_get_format_name(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), NULL);
  return self->format_name;
}

GObject *
losles_image_get_format(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), NULL);
  return self->format;
}

gsize
losles_image_get_memory_size(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), 0);

  gsize total = g_bytes_get_size(self->pixels);
  if (self->icc_profile)
    total += g_bytes_get_size(self->icc_profile);
  return total;
}
