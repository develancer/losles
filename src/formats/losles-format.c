#include "losles-format.h"

G_DEFINE_INTERFACE(LoslesFormat, losles_format, G_TYPE_OBJECT)

static void
losles_format_default_init(LoslesFormatInterface *iface)
{
  (void)iface;
}

const gchar *
losles_format_get_name(LoslesFormat *self)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), NULL);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  g_return_val_if_fail(iface->get_name != NULL, NULL);
  return iface->get_name(self);
}

gboolean
losles_format_matches(LoslesFormat *self, GBytes *encoded)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), FALSE);
  g_return_val_if_fail(encoded != NULL, FALSE);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  return iface->matches && iface->matches(self, encoded);
}

LoslesImage *
losles_format_load(LoslesFormat *self,
                   GFile *file,
                   GBytes *encoded,
                   GCancellable *cancellable,
                   GError **error)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), NULL);
  g_return_val_if_fail(G_IS_FILE(file), NULL);
  g_return_val_if_fail(encoded != NULL, NULL);

  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  g_return_val_if_fail(iface->load != NULL, NULL);
  return iface->load(self, file, encoded, cancellable, error);
}

gboolean
losles_format_supports_lossless_rotation(LoslesFormat *self)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), FALSE);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  return iface->supports_lossless_rotation &&
         iface->supports_lossless_rotation(self);
}

gboolean
losles_format_supports_lossless_crop(LoslesFormat *self)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), FALSE);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  return iface->supports_lossless_crop && iface->supports_lossless_crop(self);
}

gboolean
losles_format_adjust_crop(LoslesFormat *self,
                          LoslesImage *image,
                          const LoslesCrop *requested,
                          LoslesCrop *adjusted,
                          GError **error)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), FALSE);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  if (!iface->adjust_crop) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "This format does not support lossless cropping");
    return FALSE;
  }
  return iface->adjust_crop(self, image, requested, adjusted, error);
}

gboolean
losles_format_rotate_lossless(LoslesFormat *self,
                              LoslesImage *image,
                              GFile *destination,
                              LoslesRotation rotation,
                              GCancellable *cancellable,
                              GError **error)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), FALSE);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  if (!iface->rotate_lossless) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "This format does not support lossless rotation");
    return FALSE;
  }
  return iface->rotate_lossless(self,
                                image,
                                destination,
                                rotation,
                                cancellable,
                                error);
}

gboolean
losles_format_crop_lossless(LoslesFormat *self,
                            LoslesImage *image,
                            GFile *destination,
                            const LoslesCrop *crop,
                            GCancellable *cancellable,
                            GError **error)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT(self), FALSE);
  LoslesFormatInterface *iface = LOSLES_FORMAT_GET_IFACE(self);
  if (!iface->crop_lossless) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "This format does not support lossless cropping");
    return FALSE;
  }
  return iface->crop_lossless(self,
                              image,
                              destination,
                              crop,
                              cancellable,
                              error);
}
