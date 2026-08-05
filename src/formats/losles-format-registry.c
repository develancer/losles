#include "losles-format-registry.h"

#include "losles-jpeg-format.h"
#include "losles-png-format.h"

struct _LoslesFormatRegistry {
  GObject parent_instance;
  GPtrArray *formats;
};

G_DEFINE_FINAL_TYPE(LoslesFormatRegistry,
                    losles_format_registry,
                    G_TYPE_OBJECT)

static void
losles_format_registry_finalize(GObject *object)
{
  LoslesFormatRegistry *self = LOSLES_FORMAT_REGISTRY(object);
  g_clear_pointer(&self->formats, g_ptr_array_unref);
  G_OBJECT_CLASS(losles_format_registry_parent_class)->finalize(object);
}

static void
losles_format_registry_class_init(LoslesFormatRegistryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = losles_format_registry_finalize;
}

static void
losles_format_registry_init(LoslesFormatRegistry *self)
{
  self->formats = g_ptr_array_new_with_free_func(g_object_unref);
  g_ptr_array_add(self->formats, losles_jpeg_format_new());
  g_ptr_array_add(self->formats, losles_png_format_new());
}

LoslesFormatRegistry *
losles_format_registry_new(void)
{
  return g_object_new(LOSLES_TYPE_FORMAT_REGISTRY, NULL);
}

LoslesImage *
losles_format_registry_load(LoslesFormatRegistry *self,
                            GFile *file,
                            GCancellable *cancellable,
                            GError **error)
{
  g_return_val_if_fail(LOSLES_IS_FORMAT_REGISTRY(self), NULL);
  g_return_val_if_fail(G_IS_FILE(file), NULL);

  gchar *contents = NULL;
  gsize length = 0;
  gchar *etag = NULL;
  if (!g_file_load_contents(file,
                            cancellable,
                            &contents,
                            &length,
                            &etag,
                            error))
    return NULL;
  g_autofree gchar *source_etag = etag;

  g_autoptr(GBytes) encoded = g_bytes_new_take(contents, length);

  for (guint i = 0; i < self->formats->len; i++) {
    LoslesFormat *format = g_ptr_array_index(self->formats, i);
    if (losles_format_matches(format, encoded)) {
      LoslesImage *image =
        losles_format_load(format, file, encoded, cancellable, error);
      if (!image)
        return NULL;

      g_autoptr(GError) info_error = NULL;
      g_autoptr(GFileInfo) info =
        g_file_query_info(file,
                          LOSLES_IMAGE_SOURCE_FILE_ATTRIBUTES,
                          G_FILE_QUERY_INFO_NONE,
                          cancellable,
                          &info_error);
      if (info) {
        losles_image_set_source_file_state(image, source_etag, info);
      } else if (g_error_matches(info_error,
                                 G_IO_ERROR,
                                 G_IO_ERROR_CANCELLED)) {
        g_object_unref(image);
        g_propagate_error(error, g_steal_pointer(&info_error));
        return NULL;
      } else {
        g_debug("Could not record source file state: %s",
                info_error ? info_error->message : "unknown error");
      }
      return image;
    }
  }

  const gchar *path = g_file_peek_path(file);
  g_set_error(error,
              G_IO_ERROR,
              G_IO_ERROR_NOT_SUPPORTED,
              "Unsupported image format: %s",
              path ? path : "(non-local file)");
  return NULL;
}

gboolean
losles_format_registry_supports_file(GFile *file)
{
  g_return_val_if_fail(G_IS_FILE(file), FALSE);

  g_autofree gchar *basename = g_file_get_basename(file);
  if (!basename)
    return FALSE;

  g_autofree gchar *lower = g_utf8_strdown(basename, -1);
  return g_str_has_suffix(lower, ".jpg") ||
         g_str_has_suffix(lower, ".jpeg") ||
         g_str_has_suffix(lower, ".jpe") ||
         g_str_has_suffix(lower, ".png");
}
