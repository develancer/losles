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
  gchar *source_checksum;
  gchar *source_etag;
  gchar *source_file_id;
  gsize source_size;
  guint64 source_modified;
  guint32 source_modified_usec;
  gboolean have_source_file_state;
  guint orientation;
  gboolean has_exif_orientation;
  gboolean supports_lossless_rotation;
  gboolean supports_lossless_crop;
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
  g_clear_pointer(&self->source_checksum, g_free);
  g_clear_pointer(&self->source_etag, g_free);
  g_clear_pointer(&self->source_file_id, g_free);
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
                 GBytes *encoded_source,
                 guint orientation,
                 gboolean has_exif_orientation,
                 gboolean supports_lossless_rotation,
                 gboolean supports_lossless_crop,
                 guint jpeg_mcu_width,
                 guint jpeg_mcu_height,
                 const gchar *format_name,
                 GObject *format)
{
  g_return_val_if_fail(G_IS_FILE(file), NULL);
  g_return_val_if_fail(pixels != NULL, NULL);
  g_return_val_if_fail(encoded_source != NULL, NULL);
  g_return_val_if_fail(width > 0 && height > 0, NULL);

  LoslesImage *self = g_object_new(LOSLES_TYPE_IMAGE, NULL);
  self->file = g_object_ref(file);
  self->width = width;
  self->height = height;
  self->stride = stride;
  self->pixel_format = pixel_format;
  self->pixels = g_bytes_ref(pixels);
  self->icc_profile = icc_profile ? g_bytes_ref(icc_profile) : NULL;
  self->source_checksum =
    g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, encoded_source);
  self->source_size = g_bytes_get_size(encoded_source);
  self->orientation = orientation >= 1 && orientation <= 8 ? orientation : 1;
  self->has_exif_orientation = has_exif_orientation;
  self->supports_lossless_rotation = supports_lossless_rotation;
  self->supports_lossless_crop = supports_lossless_crop;
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

gboolean
losles_image_has_exif_orientation(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), FALSE);
  return self->has_exif_orientation;
}

gboolean
losles_image_supports_lossless_rotation(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), FALSE);
  return self->supports_lossless_rotation;
}

gboolean
losles_image_supports_lossless_crop(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), FALSE);
  return self->supports_lossless_crop;
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

const gchar *
losles_image_get_source_checksum(LoslesImage *self)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), NULL);
  return self->source_checksum;
}

void
losles_image_set_source_file_state(LoslesImage *self,
                                   const gchar *etag,
                                   GFileInfo *info)
{
  g_return_if_fail(LOSLES_IS_IMAGE(self));
  g_return_if_fail(G_IS_FILE_INFO(info));

  g_free(self->source_etag);
  self->source_etag = g_strdup(etag && *etag
                                 ? etag
                                 : g_file_info_get_attribute_string(
                                     info,
                                     G_FILE_ATTRIBUTE_ETAG_VALUE));
  g_free(self->source_file_id);
  self->source_file_id =
    g_strdup(g_file_info_get_attribute_string(info,
                                              G_FILE_ATTRIBUTE_ID_FILE));
  self->source_modified =
    g_file_info_get_attribute_uint64(info,
                                     G_FILE_ATTRIBUTE_TIME_MODIFIED);
  self->source_modified_usec =
    g_file_info_get_attribute_uint32(info,
                                     G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC);

  const goffset size = g_file_info_get_size(info);
  self->have_source_file_state =
    size >= 0 && (guint64)size == self->source_size &&
    g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}

static gboolean
source_file_info_matches(LoslesImage *self, GFileInfo *info)
{
  const goffset size = g_file_info_get_size(info);
  if (size < 0 || (guint64)size != self->source_size)
    return FALSE;

  const gchar *file_id =
    g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_ID_FILE);
  if (self->source_file_id &&
      (!file_id || !g_str_equal(self->source_file_id, file_id)))
    return FALSE;

  const gchar *etag =
    g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_ETAG_VALUE);
  if (self->source_etag || etag)
    return self->source_etag && etag && g_str_equal(self->source_etag, etag);

  if (!self->have_source_file_state)
    return FALSE;

  if (!g_file_info_has_attribute(info, G_FILE_ATTRIBUTE_TIME_MODIFIED) ||
      g_file_info_get_attribute_uint64(
        info,
        G_FILE_ATTRIBUTE_TIME_MODIFIED) != self->source_modified ||
      g_file_info_get_attribute_uint32(
        info,
        G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC) !=
        self->source_modified_usec)
    return FALSE;

  return TRUE;
}

gboolean
losles_image_source_file_is_current(LoslesImage *self,
                                    GCancellable *cancellable,
                                    GError **error)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), FALSE);

  g_autoptr(GFileInfo) info =
    g_file_query_info(self->file,
                      LOSLES_IMAGE_SOURCE_FILE_ATTRIBUTES,
                      G_FILE_QUERY_INFO_NONE,
                      cancellable,
                      error);
  return info && source_file_info_matches(self, info);
}

gboolean
losles_image_verify_source_data(LoslesImage *self,
                                const guint8 *data,
                                gsize size,
                                GError **error)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), FALSE);
  g_return_val_if_fail(data != NULL || size == 0, FALSE);

  g_autofree gchar *checksum =
    g_compute_checksum_for_data(G_CHECKSUM_SHA256, data, size);
  if (g_strcmp0(checksum, self->source_checksum) == 0)
    return TRUE;

  g_set_error_literal(error,
                      G_IO_ERROR,
                      G_IO_ERROR_WRONG_ETAG,
                      "The image has changed on disk since it was loaded. "
                      "Reload it before editing.");
  return FALSE;
}

gboolean
losles_image_verify_source_file(LoslesImage *self,
                                GCancellable *cancellable,
                                GError **error)
{
  g_return_val_if_fail(LOSLES_IS_IMAGE(self), FALSE);

  gchar *contents = NULL;
  gsize size = 0;
  if (!g_file_load_contents(self->file,
                            cancellable,
                            &contents,
                            &size,
                            NULL,
                            error))
    return FALSE;
  g_autofree gchar *owned_contents = contents;
  return losles_image_verify_source_data(self,
                                         (const guint8 *)owned_contents,
                                         size,
                                         error);
}
