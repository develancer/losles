#include "losles-png-format.h"

#include <png.h>
#include <string.h>

struct _LoslesPngFormat {
  GObject parent_instance;
};

typedef struct {
  const guint8 *data;
  gsize size;
  gsize offset;
} PngReader;

static void losles_png_format_iface_init(LoslesFormatInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
  LoslesPngFormat,
  losles_png_format,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE(LOSLES_TYPE_FORMAT, losles_png_format_iface_init))

static void
png_read_from_memory(png_structp png, png_bytep output, png_size_t length)
{
  PngReader *reader = png_get_io_ptr(png);
  if (reader->offset > reader->size || reader->size - reader->offset < length)
    png_error(png, "Unexpected end of PNG data");

  memcpy(output, reader->data + reader->offset, length);
  reader->offset += length;
}

static const gchar *
png_get_name(LoslesFormat *format)
{
  (void)format;
  return "PNG";
}

static gboolean
png_matches(LoslesFormat *format, GBytes *encoded)
{
  (void)format;
  gsize size = 0;
  const guint8 *data = g_bytes_get_data(encoded, &size);
  return size >= 8 && png_sig_cmp(data, 0, 8) == 0;
}

static LoslesImage *
png_load(LoslesFormat *format,
         GFile *file,
         GBytes *encoded,
         GCancellable *cancellable,
         GError **error)
{
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                           NULL,
                                           NULL,
                                           NULL);
  if (!png) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Could not create the PNG decoder");
    return NULL;
  }

  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, NULL, NULL);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Could not create PNG metadata storage");
    return NULL;
  }

  guchar *volatile pixels = NULL;
  GBytes *volatile icc_profile = NULL;
  volatile gboolean cancelled = FALSE;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    g_free((gpointer)pixels);
    if (icc_profile)
      g_bytes_unref((GBytes *)icc_profile);
    if (cancelled)
      g_cancellable_set_error_if_cancelled(cancellable, error);
    else
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_DATA,
                          "PNG decoding failed");
    return NULL;
  }

  gsize encoded_size = 0;
  const guint8 *encoded_data = g_bytes_get_data(encoded, &encoded_size);
  PngReader reader = {
    .data = encoded_data,
    .size = encoded_size,
  };
  png_set_read_fn(png, &reader, png_read_from_memory);
  png_read_info(png, info);

  png_uint_32 width = 0;
  png_uint_32 height = 0;
  int bit_depth = 0;
  int color_type = 0;
  int interlace_type = 0;
  png_get_IHDR(png,
               info,
               &width,
               &height,
               &bit_depth,
               &color_type,
               &interlace_type,
               NULL,
               NULL);

  if (width == 0 || height == 0) {
    png_error(png, "PNG dimensions are empty");
  }

  png_charp profile_name = NULL;
  int profile_compression = 0;
  png_bytep profile_data = NULL;
  png_uint_32 profile_size = 0;
  if (png_get_iCCP(png,
                   info,
                   &profile_name,
                   &profile_compression,
                   &profile_data,
                   &profile_size) &&
      profile_size > 0)
    icc_profile = g_bytes_new(profile_data, profile_size);

  if (bit_depth == 16)
    png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png);

  const int passes = png_set_interlace_handling(png);
  png_read_update_info(png, info);

  color_type = png_get_color_type(png, info);
  LoslesPixelFormat pixel_format;
  switch (color_type) {
  case PNG_COLOR_TYPE_GRAY:
    pixel_format = LOSLES_PIXEL_FORMAT_G8;
    break;
  case PNG_COLOR_TYPE_GRAY_ALPHA:
    pixel_format = LOSLES_PIXEL_FORMAT_GA8;
    break;
  case PNG_COLOR_TYPE_RGB:
    pixel_format = LOSLES_PIXEL_FORMAT_RGB8;
    break;
  case PNG_COLOR_TYPE_RGB_ALPHA:
    pixel_format = LOSLES_PIXEL_FORMAT_RGBA8;
    break;
  default:
    png_error(png, "Unsupported normalized PNG color type");
  }

  const png_size_t row_bytes = png_get_rowbytes(png, info);
  if (row_bytes > G_MAXUINT || height > G_MAXSIZE / row_bytes)
    png_error(png, "PNG dimensions are too large");

  pixels = g_malloc_n(height, row_bytes);
  for (int pass = 0; pass < passes; pass++) {
    for (png_uint_32 y = 0; y < height; y++) {
      if (g_cancellable_is_cancelled(cancellable))
        cancelled = TRUE;
      if (cancelled)
        png_error(png, "PNG decoding was cancelled");
      png_read_row(png,
                   (guchar *)pixels + (gsize)y * row_bytes,
                   NULL);
    }
  }
  png_read_end(png, NULL);
  png_destroy_read_struct(&png, &info, NULL);

  if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
    g_free((gpointer)pixels);
    if (icc_profile)
      g_bytes_unref((GBytes *)icc_profile);
    return NULL;
  }

  g_autoptr(GBytes) pixel_bytes =
    g_bytes_new_take((gpointer)pixels, (gsize)row_bytes * height);
  pixels = NULL;
  LoslesImage *image =
    losles_image_new(file,
                     width,
                     height,
                     row_bytes,
                     pixel_format,
                     pixel_bytes,
                     (GBytes *)icc_profile,
                     1,
                     0,
                     0,
                     "PNG",
                     G_OBJECT(format));
  if (icc_profile)
    g_bytes_unref((GBytes *)icc_profile);
  return image;
}

static gboolean
png_supports_lossless_rotation(LoslesFormat *format)
{
  (void)format;
  return FALSE;
}

static gboolean
png_supports_lossless_crop(LoslesFormat *format)
{
  (void)format;
  return FALSE;
}

static void
losles_png_format_iface_init(LoslesFormatInterface *iface)
{
  iface->get_name = png_get_name;
  iface->matches = png_matches;
  iface->load = png_load;
  iface->supports_lossless_rotation = png_supports_lossless_rotation;
  iface->supports_lossless_crop = png_supports_lossless_crop;
}

static void
losles_png_format_class_init(LoslesPngFormatClass *klass)
{
  (void)klass;
}

static void
losles_png_format_init(LoslesPngFormat *self)
{
  (void)self;
}

GObject *
losles_png_format_new(void)
{
  return g_object_new(LOSLES_TYPE_PNG_FORMAT, NULL);
}
