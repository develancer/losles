#pragma once

#include <gdk/gdk.h>

#include "losles-image.h"

G_BEGIN_DECLS

typedef struct {
  guint width;
  guint height;
  guint stride;
  LoslesPixelFormat pixel_format;
  GBytes *pixels;
  gchar *display_profile_name;
  gchar *display_profile_id;
  gboolean used_embedded_profile;
} LoslesRenderedImage;

LoslesRenderedImage *losles_rendered_image_new(guint width,
                                               guint height,
                                               guint stride,
                                               LoslesPixelFormat pixel_format,
                                               GBytes *pixels,
                                               const gchar *display_profile_name,
                                               const gchar *display_profile_id,
                                               gboolean used_embedded_profile);
void losles_rendered_image_free(LoslesRenderedImage *self);
GdkTexture *losles_rendered_image_create_texture(LoslesRenderedImage *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LoslesRenderedImage, losles_rendered_image_free)

G_END_DECLS
