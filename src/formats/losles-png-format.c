#include "losles-png-format.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <png.h>
#include <string.h>
#include <unistd.h>

struct _LoslesPngFormat {
  GObject parent_instance;
};

typedef struct {
  const guint8 *data;
  gsize size;
  gsize offset;
} PngReader;

typedef struct {
  GByteArray *data;
} PngWriter;

typedef struct {
  guint width;
  guint height;
  guint8 bit_depth;
  guint8 color_type;
  guint8 interlace_type;
  gboolean animated;
} PngContainerInfo;

typedef struct {
  gsize offset;
  gsize total_size;
  guint32 data_size;
  const guint8 *type;
} PngChunk;

typedef struct {
  guint width;
  guint height;
  guint color_type;
  guint components;
  guint stride;
  guint8 *pixels;
} EditablePng;

static void losles_png_format_iface_init(LoslesFormatInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
  LoslesPngFormat,
  losles_png_format,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE(LOSLES_TYPE_FORMAT, losles_png_format_iface_init))

static guint32
read_be32(const guint8 *data)
{
  return ((guint32)data[0] << 24) |
         ((guint32)data[1] << 16) |
         ((guint32)data[2] << 8) |
         data[3];
}

static gboolean
read_png_chunk(const guint8 *data,
               gsize size,
               gsize offset,
               PngChunk *chunk)
{
  if (offset > size || size - offset < 12)
    return FALSE;

  const guint32 data_size = read_be32(data + offset);
  if ((guint64)data_size + 12 > size - offset)
    return FALSE;

  chunk->offset = offset;
  chunk->total_size = (gsize)data_size + 12;
  chunk->data_size = data_size;
  chunk->type = data + offset + 4;
  return TRUE;
}

static gboolean
chunk_is(const PngChunk *chunk, const gchar type[4])
{
  return memcmp(chunk->type, type, 4) == 0;
}

static gboolean
inspect_png_container(const guint8 *data,
                      gsize size,
                      PngContainerInfo *info,
                      GError **error)
{
  static const guint8 signature[] = {
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n',
  };
  if (size < sizeof(signature) ||
      memcmp(data, signature, sizeof(signature)) != 0) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_DATA,
                        "The file does not have a valid PNG signature");
    return FALSE;
  }

  PngContainerInfo parsed = {0};
  gboolean saw_ihdr = FALSE;
  gboolean saw_idat = FALSE;
  gboolean saw_iend = FALSE;
  gsize offset = sizeof(signature);
  while (offset < size) {
    PngChunk chunk;
    if (!read_png_chunk(data, size, offset, &chunk)) {
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_DATA,
                          "The PNG chunk structure is truncated");
      return FALSE;
    }

    if (!saw_ihdr) {
      if (!chunk_is(&chunk, "IHDR") || chunk.data_size != 13) {
        g_set_error_literal(error,
                            G_IO_ERROR,
                            G_IO_ERROR_INVALID_DATA,
                            "The PNG does not start with a valid IHDR chunk");
        return FALSE;
      }
      const guint8 *ihdr = data + chunk.offset + 8;
      parsed.width = read_be32(ihdr);
      parsed.height = read_be32(ihdr + 4);
      parsed.bit_depth = ihdr[8];
      parsed.color_type = ihdr[9];
      parsed.interlace_type = ihdr[12];
      saw_ihdr = TRUE;
    } else if (chunk_is(&chunk, "IHDR")) {
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_DATA,
                          "The PNG contains more than one IHDR chunk");
      return FALSE;
    }

    if (chunk_is(&chunk, "acTL") ||
        chunk_is(&chunk, "fcTL") ||
        chunk_is(&chunk, "fdAT"))
      parsed.animated = TRUE;
    if (chunk_is(&chunk, "IDAT"))
      saw_idat = TRUE;
    if (chunk_is(&chunk, "IEND")) {
      if (chunk.data_size != 0) {
        g_set_error_literal(error,
                            G_IO_ERROR,
                            G_IO_ERROR_INVALID_DATA,
                            "The PNG has an invalid IEND chunk");
        return FALSE;
      }
      saw_iend = TRUE;
      break;
    }

    offset += chunk.total_size;
  }

  if (!saw_ihdr || parsed.width == 0 || parsed.height == 0 ||
      !saw_idat || !saw_iend) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_DATA,
                        "The PNG is missing required image chunks");
    return FALSE;
  }

  if (info)
    *info = parsed;
  return TRUE;
}

static gboolean
png_container_is_editable(const PngContainerInfo *info)
{
  const gboolean direct_color =
    info->color_type == PNG_COLOR_TYPE_GRAY ||
    info->color_type == PNG_COLOR_TYPE_GRAY_ALPHA ||
    info->color_type == PNG_COLOR_TYPE_RGB ||
    info->color_type == PNG_COLOR_TYPE_RGB_ALPHA;
  return info->bit_depth == 8 &&
         info->interlace_type == PNG_INTERLACE_NONE &&
         direct_color &&
         !info->animated;
}

static void
png_read_from_memory(png_structp png, png_bytep output, png_size_t length)
{
  PngReader *reader = png_get_io_ptr(png);
  if (reader->offset > reader->size || reader->size - reader->offset < length)
    png_error(png, "Unexpected end of PNG data");

  memcpy(output, reader->data + reader->offset, length);
  reader->offset += length;
}

static void
png_write_to_memory(png_structp png,
                    png_bytep input,
                    png_size_t length)
{
  PngWriter *writer = png_get_io_ptr(png);
  if (length > G_MAXUINT ||
      writer->data->len > G_MAXUINT - (guint)length)
    png_error(png, "The encoded PNG is too large");
  g_byte_array_append(writer->data, input, (guint)length);
}

static void
png_flush_memory(png_structp png)
{
  (void)png;
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

  if (width == 0 || height == 0)
    png_error(png, "PNG dimensions are empty");

  PngContainerInfo container_info = {0};
  const gboolean editable =
    inspect_png_container(encoded_data,
                          encoded_size,
                          &container_info,
                          NULL) &&
    png_container_is_editable(&container_info);

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
                     FALSE,
                     editable,
                     editable,
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
  return TRUE;
}

static gboolean
png_supports_lossless_crop(LoslesFormat *format)
{
  (void)format;
  return TRUE;
}

static gboolean
png_adjust_crop(LoslesFormat *format,
                LoslesImage *image,
                const LoslesCrop *requested,
                LoslesCrop *adjusted,
                GError **error)
{
  (void)format;
  if (!losles_image_supports_lossless_crop(image)) {
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_SUPPORTED,
      "This PNG encoding is view-only");
    return FALSE;
  }

  const guint image_width = losles_image_get_width(image);
  const guint image_height = losles_image_get_height(image);
  if (requested->width == 0 || requested->height == 0 ||
      requested->x >= image_width || requested->y >= image_height) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_ARGUMENT,
                        "The crop rectangle is empty");
    return FALSE;
  }

  const guint64 right =
    MIN((guint64)requested->x + requested->width, image_width);
  const guint64 bottom =
    MIN((guint64)requested->y + requested->height, image_height);
  adjusted->x = requested->x;
  adjusted->y = requested->y;
  adjusted->width = right - requested->x;
  adjusted->height = bottom - requested->y;
  return adjusted->width > 0 && adjusted->height > 0;
}

static void
editable_png_clear(EditablePng *image)
{
  g_clear_pointer(&image->pixels, g_free);
}

static gboolean
decode_editable_png(const guint8 *data,
                    gsize size,
                    GCancellable *cancellable,
                    EditablePng *image,
                    GError **error)
{
  PngContainerInfo container;
  if (!inspect_png_container(data, size, &container, error))
    return FALSE;
  if (!png_container_is_editable(&container)) {
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_SUPPORTED,
      "Lossless editing supports only static, non-interlaced, "
      "8-bit grayscale, grayscale-alpha, RGB, and RGBA PNG files");
    return FALSE;
  }

  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                           NULL,
                                           NULL,
                                           NULL);
  if (!png) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Could not create the PNG decoder");
    return FALSE;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_read_struct(&png, NULL, NULL);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Could not create PNG metadata storage");
    return FALSE;
  }

  guint8 *volatile pixels = NULL;
  volatile gboolean cancelled = FALSE;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_read_struct(&png, &info, NULL);
    g_free((gpointer)pixels);
    if (cancelled)
      g_cancellable_set_error_if_cancelled(cancellable, error);
    else
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_DATA,
                          "PNG decoding failed");
    return FALSE;
  }

  PngReader reader = {.data = data, .size = size};
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
  if (width != container.width || height != container.height ||
      bit_depth != 8 || color_type != container.color_type ||
      interlace_type != PNG_INTERLACE_NONE)
    png_error(png, "PNG header changed while decoding");

  png_read_update_info(png, info);
  const png_size_t row_bytes = png_get_rowbytes(png, info);
  const guint components = png_get_channels(png, info);
  if (components < 1 || components > 4 ||
      row_bytes != (png_size_t)width * components ||
      row_bytes > G_MAXUINT ||
      height > G_MAXSIZE / row_bytes)
    png_error(png, "Unsupported PNG row layout");

  pixels = g_malloc_n(height, row_bytes);
  for (png_uint_32 y = 0; y < height; y++) {
    if (g_cancellable_is_cancelled(cancellable))
      cancelled = TRUE;
    if (cancelled)
      png_error(png, "PNG decoding was cancelled");
    png_read_row(png,
                 (guint8 *)pixels + (gsize)y * row_bytes,
                 NULL);
  }
  png_read_end(png, NULL);
  png_destroy_read_struct(&png, &info, NULL);

  image->width = width;
  image->height = height;
  image->color_type = color_type;
  image->components = components;
  image->stride = row_bytes;
  image->pixels = (guint8 *)pixels;
  return TRUE;
}

static GBytes *
encode_png_pixels(const EditablePng *image,
                  GCancellable *cancellable,
                  GError **error)
{
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                            NULL,
                                            NULL,
                                            NULL);
  if (!png) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Could not create the PNG encoder");
    return NULL;
  }
  png_infop info = png_create_info_struct(png);
  if (!info) {
    png_destroy_write_struct(&png, NULL);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Could not create PNG metadata storage");
    return NULL;
  }

  GByteArray *output = g_byte_array_new();
  PngWriter writer = {.data = output};
  volatile gboolean cancelled = FALSE;
  if (setjmp(png_jmpbuf(png))) {
    png_destroy_write_struct(&png, &info);
    g_byte_array_unref(output);
    if (cancelled)
      g_cancellable_set_error_if_cancelled(cancellable, error);
    else
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_FAILED,
                          "PNG encoding failed");
    return NULL;
  }

  png_set_write_fn(png,
                   &writer,
                   png_write_to_memory,
                   png_flush_memory);
  png_set_IHDR(png,
               info,
               image->width,
               image->height,
               8,
               image->color_type,
               PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);
  png_write_info(png, info);
  for (guint y = 0; y < image->height; y++) {
    if (g_cancellable_is_cancelled(cancellable))
      cancelled = TRUE;
    if (cancelled)
      png_error(png, "PNG encoding was cancelled");
    png_write_row(png, image->pixels + (gsize)y * image->stride);
  }
  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  return g_byte_array_free_to_bytes(output);
}

static gboolean
append_bytes(GByteArray *output,
             const guint8 *data,
             gsize size,
             GError **error)
{
  if (size > G_MAXUINT || output->len > G_MAXUINT - (guint)size) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "The transformed PNG is too large to save");
    return FALSE;
  }
  g_byte_array_append(output, data, (guint)size);
  return TRUE;
}

static gboolean
append_generated_chunks(GByteArray *output,
                        GBytes *generated,
                        const gchar type[4],
                        guint *count,
                        GError **error)
{
  gsize size = 0;
  const guint8 *data = g_bytes_get_data(generated, &size);
  gsize offset = 8;
  while (offset < size) {
    PngChunk chunk;
    if (!read_png_chunk(data, size, offset, &chunk)) {
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_DATA,
                          "The generated PNG has invalid chunks");
      return FALSE;
    }
    if (chunk_is(&chunk, type)) {
      if (!append_bytes(output,
                        data + chunk.offset,
                        chunk.total_size,
                        error))
        return FALSE;
      (*count)++;
    }
    if (chunk_is(&chunk, "IEND"))
      break;
    offset += chunk.total_size;
  }
  return TRUE;
}

static GBytes *
replace_png_image_chunks(const guint8 *original,
                         gsize original_size,
                         GBytes *generated,
                         GError **error)
{
  PngContainerInfo original_info;
  if (!inspect_png_container(original,
                             original_size,
                             &original_info,
                             error))
    return NULL;
  if (original_info.animated) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "Animated PNG files are view-only");
    return NULL;
  }

  GByteArray *output = g_byte_array_new();
  if (!append_bytes(output, original, 8, error)) {
    g_byte_array_unref(output);
    return NULL;
  }

  guint generated_ihdr_count = 0;
  guint generated_idat_count = 0;
  gboolean inserted_idat = FALSE;
  gsize offset = 8;
  while (offset < original_size) {
    PngChunk chunk;
    if (!read_png_chunk(original,
                        original_size,
                        offset,
                        &chunk)) {
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_DATA,
                          "The PNG chunk structure changed unexpectedly");
      g_byte_array_unref(output);
      return NULL;
    }

    if (chunk_is(&chunk, "IHDR")) {
      if (!append_generated_chunks(output,
                                   generated,
                                   "IHDR",
                                   &generated_ihdr_count,
                                   error)) {
        g_byte_array_unref(output);
        return NULL;
      }
    } else if (chunk_is(&chunk, "IDAT")) {
      if (!inserted_idat) {
        if (!append_generated_chunks(output,
                                     generated,
                                     "IDAT",
                                     &generated_idat_count,
                                     error)) {
          g_byte_array_unref(output);
          return NULL;
        }
        inserted_idat = TRUE;
      }
    } else if (!append_bytes(output,
                             original + chunk.offset,
                             chunk.total_size,
                             error)) {
      g_byte_array_unref(output);
      return NULL;
    }

    offset += chunk.total_size;
    if (chunk_is(&chunk, "IEND")) {
      if (offset < original_size &&
          !append_bytes(output,
                        original + offset,
                        original_size - offset,
                        error)) {
        g_byte_array_unref(output);
        return NULL;
      }
      break;
    }
  }

  if (generated_ihdr_count != 1 || generated_idat_count == 0) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_FAILED,
                        "The PNG encoder did not produce required chunks");
    g_byte_array_unref(output);
    return NULL;
  }
  return g_byte_array_free_to_bytes(output);
}

static gchar *
create_temporary_file(const gchar *directory,
                      const gchar *name_template,
                      GError **error)
{
  gchar *path = g_build_filename(directory, name_template, NULL);
  const gint fd = g_mkstemp(path);
  if (fd < 0) {
    g_set_error(error,
                G_IO_ERROR,
                g_io_error_from_errno(errno),
                "Could not create a temporary file: %s",
                g_strerror(errno));
    g_free(path);
    return NULL;
  }
  close(fd);
  return path;
}

static void
remove_temporary_file(const gchar *path)
{
  if (path && g_unlink(path) != 0 && errno != ENOENT)
    g_warning("Could not remove temporary file %s: %s",
              path,
              g_strerror(errno));
}

static gchar *
create_source_backup(const gchar *source_path,
                     const gchar *directory,
                     GCancellable *cancellable,
                     GError **error)
{
  g_autofree gchar *backup_path =
    create_temporary_file(directory, ".losles-backup-XXXXXX", error);
  if (!backup_path)
    return NULL;
  if (g_unlink(backup_path) != 0) {
    g_set_error(error,
                G_IO_ERROR,
                g_io_error_from_errno(errno),
                "Could not prepare the crop safety backup: %s",
                g_strerror(errno));
    return NULL;
  }

  if (link(source_path, backup_path) == 0)
    return g_steal_pointer(&backup_path);

  g_autoptr(GFile) source = g_file_new_for_path(source_path);
  g_autoptr(GFile) backup = g_file_new_for_path(backup_path);
  if (!g_file_copy(source,
                   backup,
                   G_FILE_COPY_ALL_METADATA,
                   cancellable,
                   NULL,
                   NULL,
                   error)) {
    remove_temporary_file(backup_path);
    return NULL;
  }
  return g_steal_pointer(&backup_path);
}

static void
copy_source_permissions(const gchar *source_path,
                        const gchar *temporary_path)
{
  GStatBuf source_stat;
  if (g_stat(source_path, &source_stat) == 0 &&
      g_chmod(temporary_path, source_stat.st_mode & 07777) != 0) {
    g_debug("Could not preserve PNG permissions on %s: %s",
            temporary_path,
            g_strerror(errno));
  }
}

static gboolean
install_transformed_file(GFile *source,
                         GFile *destination,
                         const gchar *source_path,
                         const gchar *temporary_path,
                         gboolean trash_source,
                         GCancellable *cancellable,
                         GError **error)
{
  g_autoptr(GFile) temporary = g_file_new_for_path(temporary_path);
  if (!trash_source) {
    return g_file_move(temporary,
                       destination,
                       G_FILE_COPY_OVERWRITE,
                       cancellable,
                       NULL,
                       NULL,
                       error);
  }

  g_autofree gchar *source_directory = g_path_get_dirname(source_path);
  g_autofree gchar *backup_path =
    create_source_backup(source_path,
                         source_directory,
                         cancellable,
                         error);
  if (!backup_path)
    return FALSE;
  if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
    remove_temporary_file(backup_path);
    return FALSE;
  }

  if (!g_file_trash(source, cancellable, error)) {
    remove_temporary_file(backup_path);
    return FALSE;
  }

  g_autoptr(GError) install_error = NULL;
  if (!g_file_move(temporary,
                   source,
                   G_FILE_COPY_NONE,
                   NULL,
                   NULL,
                   NULL,
                   &install_error)) {
    g_autoptr(GFile) backup = g_file_new_for_path(backup_path);
    g_autoptr(GError) restore_error = NULL;
    if (g_file_move(backup,
                    source,
                    G_FILE_COPY_NONE,
                    NULL,
                    NULL,
                    NULL,
                    &restore_error)) {
      g_set_error(error,
                  G_IO_ERROR,
                  G_IO_ERROR_FAILED,
                  "Could not install the transformed image after moving the "
                  "original to Trash: %s. The original was restored; another "
                  "recoverable copy remains in Trash.",
                  install_error->message);
    } else {
      g_set_error(error,
                  G_IO_ERROR,
                  G_IO_ERROR_FAILED,
                  "Could not install the transformed image after moving the "
                  "original to Trash: %s. Automatic restoration also failed: "
                  "%s. The safety backup remains at %s.",
                  install_error->message,
                  restore_error->message,
                  backup_path);
    }
    return FALSE;
  }

  remove_temporary_file(backup_path);
  return TRUE;
}

static gboolean
run_png_transform(LoslesImage *image,
                  GFile *destination,
                  const LoslesRotation *rotation,
                  const LoslesCrop *crop,
                  gboolean trash_source,
                  GCancellable *cancellable,
                  GError **error)
{
  GFile *source = losles_image_get_file(image);
  g_autofree gchar *source_path = g_file_get_path(source);
  g_autofree gchar *destination_path = g_file_get_path(destination);
  if (!source_path || !destination_path) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "Lossless PNG editing requires local files");
    return FALSE;
  }

  const gboolean in_place = g_file_equal(source, destination);
  if (in_place) {
    g_autoptr(GFileInfo) info =
      g_file_query_info(source,
                        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK,
                        G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                        cancellable,
                        error);
    if (!info)
      return FALSE;
    if (g_file_info_get_file_type(info) != G_FILE_TYPE_REGULAR ||
        g_file_info_get_is_symlink(info)) {
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_NOT_SUPPORTED,
                          "In-place editing requires a regular local file");
      return FALSE;
    }
  }

  gchar *loaded = NULL;
  gsize loaded_size = 0;
  if (!g_file_load_contents(source,
                            cancellable,
                            &loaded,
                            &loaded_size,
                            NULL,
                            error))
    return FALSE;
  g_autofree gchar *source_data = loaded;

  EditablePng decoded = {0};
  if (!decode_editable_png((const guint8 *)source_data,
                           loaded_size,
                           cancellable,
                           &decoded,
                           error))
    return FALSE;

  EditablePng transformed = {
    .color_type = decoded.color_type,
    .components = decoded.components,
  };
  if (rotation) {
    transformed.width = decoded.height;
    transformed.height = decoded.width;
  } else {
    g_assert(crop != NULL);
    const guint64 right = (guint64)crop->x + crop->width;
    const guint64 bottom = (guint64)crop->y + crop->height;
    if (crop->width == 0 || crop->height == 0 ||
        crop->x >= decoded.width || crop->y >= decoded.height ||
        right > decoded.width || bottom > decoded.height) {
      editable_png_clear(&decoded);
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_INVALID_ARGUMENT,
                          "The crop rectangle is outside the current PNG");
      return FALSE;
    }
    transformed.width = crop->width;
    transformed.height = crop->height;
  }

  if (transformed.width > G_MAXUINT / transformed.components ||
      transformed.height >
        G_MAXSIZE / ((gsize)transformed.width * transformed.components)) {
    editable_png_clear(&decoded);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "The transformed PNG dimensions are too large");
    return FALSE;
  }
  transformed.stride = transformed.width * transformed.components;
  transformed.pixels =
    g_malloc_n(transformed.height, transformed.stride);

  if (rotation) {
    for (guint y = 0; y < decoded.height; y++) {
      if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
        editable_png_clear(&decoded);
        editable_png_clear(&transformed);
        return FALSE;
      }
      for (guint x = 0; x < decoded.width; x++) {
        const guint destination_x =
          *rotation == LOSLES_ROTATE_RIGHT ? decoded.height - 1 - y : y;
        const guint destination_y =
          *rotation == LOSLES_ROTATE_RIGHT ? x : decoded.width - 1 - x;
        memcpy(transformed.pixels +
                 (gsize)destination_y * transformed.stride +
                 (gsize)destination_x * transformed.components,
               decoded.pixels +
                 (gsize)y * decoded.stride +
                 (gsize)x * decoded.components,
               decoded.components);
      }
    }
  } else {
    for (guint y = 0; y < crop->height; y++) {
      memcpy(transformed.pixels + (gsize)y * transformed.stride,
             decoded.pixels +
               (gsize)(crop->y + y) * decoded.stride +
               (gsize)crop->x * decoded.components,
             transformed.stride);
    }
  }

  g_autoptr(GBytes) generated =
    encode_png_pixels(&transformed, cancellable, error);
  editable_png_clear(&decoded);
  editable_png_clear(&transformed);
  if (!generated)
    return FALSE;

  g_autoptr(GBytes) output =
    replace_png_image_chunks((const guint8 *)source_data,
                             loaded_size,
                             generated,
                             error);
  if (!output)
    return FALSE;
  if (g_cancellable_set_error_if_cancelled(cancellable, error))
    return FALSE;

  g_autofree gchar *destination_directory =
    g_path_get_dirname(destination_path);
  g_autofree gchar *temporary_path =
    create_temporary_file(destination_directory,
                          ".losles-output-XXXXXX",
                          error);
  if (!temporary_path)
    return FALSE;

  gsize output_size = 0;
  const guint8 *output_data = g_bytes_get_data(output, &output_size);
  if (output_size > G_MAXSSIZE ||
      !g_file_set_contents(temporary_path,
                           (const gchar *)output_data,
                           (gssize)output_size,
                           error)) {
    if (output_size > G_MAXSSIZE)
      g_set_error_literal(error,
                          G_IO_ERROR,
                          G_IO_ERROR_NO_SPACE,
                          "The transformed PNG is too large to save");
    remove_temporary_file(temporary_path);
    return FALSE;
  }
  copy_source_permissions(source_path, temporary_path);

  const gboolean installed =
    install_transformed_file(source,
                             destination,
                             source_path,
                             temporary_path,
                             trash_source,
                             cancellable,
                             error);
  if (!installed)
    remove_temporary_file(temporary_path);
  return installed;
}

static gboolean
png_rotate_lossless(LoslesFormat *format,
                    LoslesImage *image,
                    GFile *destination,
                    LoslesRotation rotation,
                    GCancellable *cancellable,
                    GError **error)
{
  (void)format;
  if (!losles_image_supports_lossless_rotation(image)) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "This PNG encoding is view-only");
    return FALSE;
  }
  return run_png_transform(image,
                           destination,
                           &rotation,
                           NULL,
                           FALSE,
                           cancellable,
                           error);
}

static gboolean
png_crop_lossless(LoslesFormat *format,
                  LoslesImage *image,
                  GFile *destination,
                  const LoslesCrop *crop,
                  GCancellable *cancellable,
                  GError **error)
{
  LoslesCrop adjusted;
  if (!png_adjust_crop(format, image, crop, &adjusted, error))
    return FALSE;
  return run_png_transform(
    image,
    destination,
    NULL,
    &adjusted,
    g_file_equal(losles_image_get_file(image), destination),
    cancellable,
    error);
}

static void
losles_png_format_iface_init(LoslesFormatInterface *iface)
{
  iface->get_name = png_get_name;
  iface->matches = png_matches;
  iface->load = png_load;
  iface->supports_lossless_rotation = png_supports_lossless_rotation;
  iface->supports_lossless_crop = png_supports_lossless_crop;
  iface->adjust_crop = png_adjust_crop;
  iface->rotate_lossless = png_rotate_lossless;
  iface->crop_lossless = png_crop_lossless;
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
