#include "losles-jpeg-format.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <string.h>
#include <turbojpeg.h>

#include "losles-jpeg-metadata.h"
#include "../losles-platform.h"

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
  guint orientation = 1;
  const gboolean has_exif_orientation =
    losles_jpeg_metadata_read_orientation(encoded, &orientation);

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
                     has_exif_orientation,
                     TRUE,
                     TRUE,
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
jpeg_supports_lossless_orientation_normalization(LoslesFormat *format)
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
      "Use Normalize first.");
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
  g_close(fd, NULL);
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

  /*
   * A hard link is fast and retains the exact original inode while GIO moves
   * the source directory entry to Trash. Fall back to a metadata-preserving
   * copy for filesystems that do not support hard links.
   */
  g_autoptr(GError) link_error = NULL;
  if (losles_platform_create_hard_link(source_path,
                                       backup_path,
                                       &link_error))
    return g_steal_pointer(&backup_path);

  g_debug("Could not hard-link the crop backup: %s; copying instead",
          link_error ? link_error->message : "unknown error");
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
  losles_platform_copy_file_permissions(source_path,
                                        temporary_path);
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

  /*
   * Do not allow cancellation to interrupt the commit phase. Once the
   * original has entered Trash, either the transformed file or the safety
   * backup must be installed at the original path.
   */
  if (!losles_platform_trash_file(source,
                                  cancellable,
                                  error)) {
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
run_turbojpeg_transform(LoslesImage *image,
                        GFile *destination,
                        gint operation,
                        const LoslesCrop *crop,
                        gboolean normalize_orientation,
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
                        "Lossless JPEG editing requires local files");
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

  gchar *source_data = NULL;
  gsize source_size = 0;
  if (!g_file_load_contents(source,
                            cancellable,
                            &source_data,
                            &source_size,
                            NULL,
                            error))
    return FALSE;
  g_autofree gchar *source_bytes = source_data;
  if (source_size > G_MAXULONG) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "The JPEG file is too large for TurboJPEG");
    return FALSE;
  }

  tjhandle transformer = tjInitTransform();
  if (!transformer) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "Could not initialize TurboJPEG: %s",
                tjGetErrorStr2(NULL));
    return FALSE;
  }

  tjtransform transform = {0};
  transform.op = operation;
  transform.options = TJXOPT_PERFECT;
  if (crop) {
    transform.options |= TJXOPT_CROP;
    transform.r.x = crop->x;
    transform.r.y = crop->y;
    transform.r.w = crop->width;
    transform.r.h = crop->height;
  }

  unsigned char *output = NULL;
  unsigned long output_size = 0;
  const gint transform_result =
    tjTransform(transformer,
                (const unsigned char *)source_bytes,
                (unsigned long)source_size,
                1,
                &output,
                &output_size,
                &transform,
                TJFLAG_STOPONWARNING);
  if (transform_result != 0) {
    g_set_error(error,
                G_IO_ERROR,
                G_IO_ERROR_FAILED,
                "TurboJPEG could not perform a perfect lossless transform: %s",
                tjGetErrorStr2(transformer));
    tjDestroy(transformer);
    tjFree(output);
    return FALSE;
  }
  tjDestroy(transformer);

  if (output_size > G_MAXSSIZE) {
    tjFree(output);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "The transformed JPEG is too large to save");
    return FALSE;
  }
  if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
    tjFree(output);
    return FALSE;
  }

  g_autofree gchar *destination_directory =
    g_path_get_dirname(destination_path);
  g_autofree gchar *temporary_path =
    create_temporary_file(destination_directory,
                          ".losles-output-XXXXXX",
                          error);
  if (!temporary_path) {
    tjFree(output);
    return FALSE;
  }

  const gboolean wrote_output =
    g_file_set_contents(temporary_path,
                        (const gchar *)output,
                        (gssize)output_size,
                        error);
  tjFree(output);
  if (!wrote_output) {
    remove_temporary_file(temporary_path);
    return FALSE;
  }
  if (normalize_orientation &&
      !losles_jpeg_metadata_set_orientation_in_file(temporary_path,
                                                    1,
                                                    error)) {
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
jpeg_rotate_lossless(LoslesFormat *format,
                     LoslesImage *image,
                     GFile *destination,
                     LoslesRotation rotation,
                     GCancellable *cancellable,
                     GError **error)
{
  (void)format;
  const guint orientation = losles_image_get_orientation(image);
  const gboolean mirrored =
    orientation == 2 || orientation == 4 ||
    orientation == 5 || orientation == 7;
  const gint operation = rotation == LOSLES_ROTATE_RIGHT
                           ? (mirrored ? TJXOP_ROT270 : TJXOP_ROT90)
                           : (mirrored ? TJXOP_ROT90 : TJXOP_ROT270);

  return run_turbojpeg_transform(
    image,
    destination,
    operation,
    NULL,
    FALSE,
    FALSE,
    cancellable,
    error);
}

static gboolean
jpeg_normalize_orientation_lossless(LoslesFormat *format,
                                    LoslesImage *image,
                                    GFile *destination,
                                    GCancellable *cancellable,
                                    GError **error)
{
  (void)format;
  if (!losles_image_has_exif_orientation(image) ||
      losles_image_get_orientation(image) == 1) {
    g_set_error_literal(
      error,
      G_IO_ERROR,
      G_IO_ERROR_NOT_SUPPORTED,
      "This JPEG has no non-default EXIF orientation to normalize");
    return FALSE;
  }

  static const gint operations[] = {
    TJXOP_NONE,
    TJXOP_NONE,
    TJXOP_HFLIP,
    TJXOP_ROT180,
    TJXOP_VFLIP,
    TJXOP_TRANSPOSE,
    TJXOP_ROT90,
    TJXOP_TRANSVERSE,
    TJXOP_ROT270,
  };
  const guint orientation = losles_image_get_orientation(image);
  return run_turbojpeg_transform(image,
                                 destination,
                                 operations[orientation],
                                 NULL,
                                 TRUE,
                                 FALSE,
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

  return run_turbojpeg_transform(image,
                                 destination,
                                 TJXOP_NONE,
                                 &adjusted,
                                 FALSE,
                                 g_file_equal(losles_image_get_file(image),
                                              destination),
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
  iface->supports_lossless_orientation_normalization =
    jpeg_supports_lossless_orientation_normalization;
  iface->supports_lossless_crop = jpeg_supports_lossless_crop;
  iface->adjust_crop = jpeg_adjust_crop;
  iface->rotate_lossless = jpeg_rotate_lossless;
  iface->normalize_orientation_lossless =
    jpeg_normalize_orientation_lossless;
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
