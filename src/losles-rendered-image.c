#include "losles-rendered-image.h"

LoslesRenderedImage *
losles_rendered_image_new(guint width,
                          guint height,
                          guint stride,
                          LoslesPixelFormat pixel_format,
                          GBytes *pixels,
                          const gchar *display_profile_name,
                          const gchar *display_profile_id,
                          gboolean used_embedded_profile)
{
  g_return_val_if_fail(width > 0 && height > 0, NULL);
  g_return_val_if_fail(pixels != NULL, NULL);

  LoslesRenderedImage *self = g_new0(LoslesRenderedImage, 1);
  self->width = width;
  self->height = height;
  self->stride = stride;
  self->pixel_format = pixel_format;
  self->pixels = g_bytes_ref(pixels);
  self->display_profile_name = g_strdup(display_profile_name);
  self->display_profile_id = g_strdup(display_profile_id);
  self->used_embedded_profile = used_embedded_profile;
  return self;
}

void
losles_rendered_image_free(LoslesRenderedImage *self)
{
  if (!self)
    return;

  g_clear_pointer(&self->pixels, g_bytes_unref);
  g_clear_pointer(&self->display_profile_name, g_free);
  g_clear_pointer(&self->display_profile_id, g_free);
  g_free(self);
}

GdkTexture *
losles_rendered_image_create_texture(LoslesRenderedImage *self)
{
  g_return_val_if_fail(self != NULL, NULL);

  GdkMemoryFormat format =
    self->pixel_format == LOSLES_PIXEL_FORMAT_RGBA8
      ? GDK_MEMORY_R8G8B8A8
      : GDK_MEMORY_R8G8B8;

  return GDK_TEXTURE(gdk_memory_texture_new(self->width,
                                            self->height,
                                            format,
                                            self->pixels,
                                            self->stride));
}
