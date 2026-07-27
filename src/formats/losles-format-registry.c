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
  if (!g_file_load_contents(file,
                            cancellable,
                            &contents,
                            &length,
                            NULL,
                            error))
    return NULL;

  g_autoptr(GBytes) encoded = g_bytes_new_take(contents, length);

  for (guint i = 0; i < self->formats->len; i++) {
    LoslesFormat *format = g_ptr_array_index(self->formats, i);
    if (losles_format_matches(format, encoded))
      return losles_format_load(format, file, encoded, cancellable, error);
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
