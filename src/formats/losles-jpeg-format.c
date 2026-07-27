#include "losles-jpeg-format.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>

#include "losles-jpeg-metadata.h"

struct _LoslesJpegFormat {
  GObject parent_instance;
};

typedef struct {
  struct jpeg_error_mgr parent;
  jmp_buf jump;
  gchar message[JMSG_LENGTH_MAX];
} JpegError;

static void losles_jpeg_format_iface_init(LoslesFormatInterface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE(
  LoslesJpegFormat,
  losles_jpeg_format,
  G_TYPE_OBJECT,
  G_IMPLEMENT_INTERFACE(LOSLES_TYPE_FORMAT, losles_jpeg_format_iface_init))

static void
jpeg_error_exit(j_common_ptr cinfo)
{
  JpegError *error = (JpegError *)cinfo->err;
  (*cinfo->err->format_message)(cinfo, error->message);
  longjmp(error->jump, 1);
}

static const gchar *
jpeg_get_name(LoslesFormat *format)
{
  (void)format;
  return "JPEG";
}

static gboolean
jpeg_matches(LoslesFormat *format, GBytes *encoded)
{
  (void)format;
  gsize size = 0;
  const guint8 *data = g_bytes_get_data(encoded, &size);
  return size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff;
}

static GBytes *
extract_icc_profile(struct jpeg_decompress_struct *cinfo)
{
  enum { ICC_HEADER_SIZE = 14, ICC_CHUNK_COUNT = 255 };
  const guint8 *chunks[ICC_CHUNK_COUNT] = {0};
  gsize chunk_sizes[ICC_CHUNK_COUNT] = {0};
  guint expected_count = 0;

  for (jpeg_saved_marker_ptr marker = cinfo->marker_list;
       marker;
       marker = marker->next) {
    if (marker->marker != JPEG_APP0 + 2 ||
        marker->data_length < ICC_HEADER_SIZE ||
        memcmp(marker->data, "ICC_PROFILE\0", 12) != 0)
      continue;

    const guint sequence = marker->data[12];
    const guint count = marker->data[13];
    if (sequence == 0 || count == 0 || sequence > count)
      return NULL;
    if (expected_count != 0 && expected_count != count)
      return NULL;

    expected_count = count;
    chunks[sequence - 1] = marker->data + ICC_HEADER_SIZE;
    chunk_sizes[sequence - 1] = marker->data_length - ICC_HEADER_SIZE;
  }

  if (expected_count == 0)
    return NULL;

  gsize total = 0;
  for (guint i = 0; i < expected_count; i++) {
    if (!chunks[i] || G_MAXSIZE - total < chunk_sizes[i])
      return NULL;
    total += chunk_sizes[i];
  }

  guint8 *profile = g_malloc(total);
  gsize offset = 0;
  for (guint i = 0; i < expected_count; i++) {
    memcpy(profile + offset, chunks[i], chunk_sizes[i]);
    offset += chunk_sizes[i];
  }
  return g_bytes_new_take(profile, total);
}

static LoslesImage *
jpeg_load(LoslesFormat *format,
          GFile *file,
          GBytes *encoded,
          GCancellable *cancellable,
          GError **error)
{
  struct jpeg_decompress_struct cinfo = {0};
  JpegError jpeg_error = {0};
  volatile gboolean created = FALSE;
  guchar *volatile pixels = NULL;
  GBytes *volatile icc_profile = NULL;

  cinfo.err = jpeg_std_error(&jpeg_error.parent);
  jpeg_error.parent.error_exit = jpeg_error_exit;
  if (setjmp(jpeg_error.jump)) {
    if (created)
      jpeg_destroy_decompress(&cinfo);
    g_free((gpointer)pixels);
    if (icc_profile)
      g_bytes_unref((GBytes *)icc_profile);
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_INVALID_DATA,
                "JPEG decoding failed: %s",
                jpeg_error.message);
    return NULL;
  }

  jpeg_create_decompress(&cinfo);
  created = TRUE;
  jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xffff);
  jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xffff);

  gsize encoded_size = 0;
  const guchar *encoded_data = g_bytes_get_data(encoded, &encoded_size);
  jpeg_mem_src(&cinfo, encoded_data, encoded_size);
  jpeg_read_header(&cinfo, TRUE);

  const guint mcu_width = cinfo.max_h_samp_factor * DCTSIZE;
  const guint mcu_height = cinfo.max_v_samp_factor * DCTSIZE;
  icc_profile = extract_icc_profile(&cinfo);

  if (cinfo.jpeg_color_space == JCS_CMYK ||
      cinfo.jpeg_color_space == JCS_YCCK) {
    jpeg_destroy_decompress(&cinfo);
    if (icc_profile)
      g_bytes_unref((GBytes *)icc_profile);
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_SUPPORTED,
      "CMYK JPEG display is not implemented yet");
    return NULL;
  }

  const gboolean grayscale =
    cinfo.jpeg_color_space == JCS_GRAYSCALE;
  const guint components = grayscale ? 1 : 3;
  cinfo.out_color_space = grayscale ? JCS_GRAYSCALE : JCS_RGB;
  cinfo.do_fancy_upsampling = TRUE;
  jpeg_start_decompress(&cinfo);

  if (cinfo.output_width == 0 ||
      cinfo.output_height == 0 ||
      (guint)cinfo.output_components != components ||
      cinfo.output_width > G_MAXUINT / components ||
      cinfo.output_height >
        G_MAXSIZE / (cinfo.output_width * components)) {
    jpeg_destroy_decompress(&cinfo);
    if (icc_profile)
      g_bytes_unref((GBytes *)icc_profile);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "JPEG dimensions are too large");
    return NULL;
  }

  const guint width = cinfo.output_width;
  const guint height = cinfo.output_height;
  const guint stride = width * components;
  pixels = g_malloc_n(height, stride);

  while (cinfo.output_scanline < cinfo.output_height) {
    if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
      jpeg_abort_decompress(&cinfo);
      jpeg_destroy_decompress(&cinfo);
      g_free((gpointer)pixels);
      if (icc_profile)
        g_bytes_unref((GBytes *)icc_profile);
      return NULL;
    }

    JSAMPROW row = (guchar *)pixels + (gsize)cinfo.output_scanline * stride;
    jpeg_read_scanlines(&cinfo, &row, 1);
  }

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  g_autoptr(GBytes) pixel_bytes =
    g_bytes_new_take((gpointer)pixels, (gsize)stride * height);
  pixels = NULL;
  const guint orientation = losles_jpeg_metadata_get_orientation(encoded);

  LoslesImage *image =
    losles_image_new(file,
                     width,
                     height,
                     stride,
                     grayscale ? LOSLES_PIXEL_FORMAT_G8
                               : LOSLES_PIXEL_FORMAT_RGB8,
                     pixel_bytes,
                     (GBytes *)icc_profile,
                     orientation,
                     mcu_width,
                     mcu_height,
                     "JPEG",
                     G_OBJECT(format));
  if (icc_profile)
    g_bytes_unref((GBytes *)icc_profile);
  return image;
}

static gboolean
jpeg_supports_lossless_rotation(LoslesFormat *format)
{
  (void)format;
  return TRUE;
}

static gboolean
jpeg_supports_lossless_crop(LoslesFormat *format)
{
  (void)format;
  return TRUE;
}

static gboolean
jpeg_adjust_crop(LoslesFormat *format,
                 LoslesImage *image,
                 const LoslesCrop *requested,
                 LoslesCrop *adjusted,
                 GError **error)
{
  (void)format;
  if (losles_image_get_orientation(image) != 1) {
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_SUPPORTED,
      "Lossless crop currently requires EXIF orientation 1. "
      "Save a losslessly rotated copy first.");
    return FALSE;
  }

  const guint image_width = losles_image_get_width(image);
  const guint image_height = losles_image_get_height(image);
  const guint mcu_width = MAX(losles_image_get_jpeg_mcu_width(image), 1);
  const guint mcu_height = MAX(losles_image_get_jpeg_mcu_height(image), 1);
  if (requested->width == 0 || requested->height == 0 ||
      requested->x >= image_width || requested->y >= image_height) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_ARGUMENT,
                        "The crop rectangle is empty");
    return FALSE;
  }

  const guint64 requested_right =
    MIN((guint64)requested->x + requested->width, image_width);
  const guint64 requested_bottom =
    MIN((guint64)requested->y + requested->height, image_height);

  adjusted->x = (requested->x / mcu_width) * mcu_width;
  adjusted->y = (requested->y / mcu_height) * mcu_height;

  guint64 right = ((requested_right + mcu_width - 1) / mcu_width) * mcu_width;
  guint64 bottom =
    ((requested_bottom + mcu_height - 1) / mcu_height) * mcu_height;
  right = MIN(right, image_width);
  bottom = MIN(bottom, image_height);
  adjusted->width = right - adjusted->x;
  adjusted->height = bottom - adjusted->y;
  return adjusted->width > 0 && adjusted->height > 0;
}

static gboolean
run_jpegtran(LoslesImage *image,
             GFile *destination,
             const gchar *operation,
             const gchar *argument,
             gboolean reset_orientation,
             GCancellable *cancellable,
             GError **error)
{
  g_autofree gchar *jpegtran = g_find_program_in_path("jpegtran");
  if (!jpegtran) {
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_FOUND,
      "jpegtran is required. Install the Ubuntu package "
      "“libjpeg-turbo-progs”.");
    return FALSE;
  }

  g_autofree gchar *source_path =
    g_file_get_path(losles_image_get_file(image));
  g_autofree gchar *destination_path = g_file_get_path(destination);
  if (!source_path || !destination_path) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "Lossless JPEG editing requires local files");
    return FALSE;
  }

  g_autofree gchar *destination_dir = g_path_get_dirname(destination_path);
  g_autofree gchar *temporary_path =
    g_build_filename(destination_dir, ".losles-XXXXXX", NULL);
  const gint temporary_fd = g_mkstemp(temporary_path);
  if (temporary_fd < 0) {
    g_set_error(error,
                G_IO_ERROR,
                g_io_error_from_errno(errno),
                "Could not create temporary output: %s",
                g_strerror(errno));
    return FALSE;
  }
  close(temporary_fd);

  g_autoptr(GPtrArray) arguments = g_ptr_array_new();
  g_ptr_array_add(arguments, jpegtran);
  g_ptr_array_add(arguments, "-copy");
  g_ptr_array_add(arguments, "all");
  if (operation) {
    g_ptr_array_add(arguments, "-perfect");
    g_ptr_array_add(arguments, (gpointer)operation);
    g_ptr_array_add(arguments, (gpointer)argument);
  }
  g_ptr_array_add(arguments, "-outfile");
  g_ptr_array_add(arguments, temporary_path);
  g_ptr_array_add(arguments, source_path);
  g_ptr_array_add(arguments, NULL);

  g_autoptr(GSubprocessLauncher) launcher =
    g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDERR_PIPE);
  g_autoptr(GSubprocess) process =
    g_subprocess_launcher_spawnv(
      launcher,
      (const gchar *const *)arguments->pdata,
      error);
  if (!process) {
    g_unlink(temporary_path);
    return FALSE;
  }

  g_autofree gchar *stderr_text = NULL;
  if (!g_subprocess_communicate_utf8(process,
                                     NULL,
                                     cancellable,
                                     NULL,
                                     &stderr_text,
                                     error) ||
      !g_subprocess_get_successful(process)) {
    if (error && !*error) {
      g_set_error(error,
                  G_IO_ERROR,
                  G_IO_ERROR_FAILED,
                  "jpegtran could not perform a perfect lossless transform: %s",
                  stderr_text && *stderr_text ? stderr_text : "unknown error");
    }
    g_unlink(temporary_path);
    return FALSE;
  }

  if (reset_orientation &&
      !losles_jpeg_metadata_set_orientation_in_file(temporary_path, 1, error)) {
    g_unlink(temporary_path);
    return FALSE;
  }

  g_autoptr(GFile) temporary = g_file_new_for_path(temporary_path);
  if (!g_file_move(temporary,
                   destination,
                   G_FILE_COPY_OVERWRITE,
                   cancellable,
                   NULL,
                   NULL,
                   error)) {
    g_unlink(temporary_path);
    return FALSE;
  }

  return TRUE;
}

static gboolean
jpeg_rotate_lossless(LoslesFormat *format,
                     LoslesImage *image,
                     GFile *destination,
                     LoslesRotation rotation,
                     GCancellable *cancellable,
                     GError **error)
{
  (void)format;
  const guint orientation = losles_image_get_orientation(image);
  gint current_degrees;
  switch (orientation) {
  case 1:
    current_degrees = 0;
    break;
  case 3:
    current_degrees = 180;
    break;
  case 6:
    current_degrees = 90;
    break;
  case 8:
    current_degrees = 270;
    break;
  default:
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_SUPPORTED,
      "Mirrored EXIF orientations are displayed correctly but are not yet "
      "supported by the lossless rotation writer");
    return FALSE;
  }

  const gint delta = rotation == LOSLES_ROTATE_RIGHT ? 90 : -90;
  const gint result_degrees = (current_degrees + delta + 360) % 360;
  if (result_degrees == 0) {
    return run_jpegtran(image,
                        destination,
                        NULL,
                        NULL,
                        TRUE,
                        cancellable,
                        error);
  }

  g_autofree gchar *degrees = g_strdup_printf("%d", result_degrees);
  return run_jpegtran(image,
                      destination,
                      "-rotate",
                      degrees,
                      TRUE,
                      cancellable,
                      error);
}

static gboolean
jpeg_crop_lossless(LoslesFormat *format,
                   LoslesImage *image,
                   GFile *destination,
                   const LoslesCrop *crop,
                   GCancellable *cancellable,
                   GError **error)
{
  LoslesCrop adjusted;
  if (!jpeg_adjust_crop(format, image, crop, &adjusted, error))
    return FALSE;

  g_autofree gchar *geometry =
    g_strdup_printf("%ux%u+%u+%u",
                    adjusted.width,
                    adjusted.height,
                    adjusted.x,
                    adjusted.y);
  return run_jpegtran(image,
                      destination,
                      "-crop",
                      geometry,
                      FALSE,
                      cancellable,
                      error);
}

static void
losles_jpeg_format_iface_init(LoslesFormatInterface *iface)
{
  iface->get_name = jpeg_get_name;
  iface->matches = jpeg_matches;
  iface->load = jpeg_load;
  iface->supports_lossless_rotation = jpeg_supports_lossless_rotation;
  iface->supports_lossless_crop = jpeg_supports_lossless_crop;
  iface->adjust_crop = jpeg_adjust_crop;
  iface->rotate_lossless = jpeg_rotate_lossless;
  iface->crop_lossless = jpeg_crop_lossless;
}

static void
losles_jpeg_format_class_init(LoslesJpegFormatClass *klass)
{
  (void)klass;
}

static void
losles_jpeg_format_init(LoslesJpegFormat *self)
{
  (void)self;
}

GObject *
losles_jpeg_format_new(void)
{
  return g_object_new(LOSLES_TYPE_JPEG_FORMAT, NULL);
}
