#include <glib.h>
#include <glib/gstdio.h>
#include <jpeglib.h>
#include <lcms2.h>
#include <png.h>
#include <stdio.h>
#include <string.h>

#include "../src/formats/losles-format-registry.h"
#include "../src/formats/losles-format.h"
#include "../src/losles-color-manager.h"
#include "../src/losles-image.h"
#include "../src/losles-rendered-image.h"

static GBytes *
make_srgb_profile(void)
{
  cmsHPROFILE profile = cmsCreate_sRGBProfile();
  g_assert_nonnull(profile);

  cmsUInt32Number size = 0;
  g_assert_true(cmsSaveProfileToMem(profile, NULL, &size));
  guint8 *data = g_malloc(size);
  g_assert_true(cmsSaveProfileToMem(profile, data, &size));
  cmsCloseProfile(profile);
  return g_bytes_new_take(data, size);
}

static GBytes *
make_gray_profile(void)
{
  cmsCIExyY white_point;
  g_assert_true(cmsWhitePointFromTemp(&white_point, 6504));
  cmsToneCurve *gamma = cmsBuildGamma(NULL, 2.2);
  g_assert_nonnull(gamma);
  cmsHPROFILE profile = cmsCreateGrayProfile(&white_point, gamma);
  cmsFreeToneCurve(gamma);
  g_assert_nonnull(profile);

  cmsUInt32Number size = 0;
  g_assert_true(cmsSaveProfileToMem(profile, NULL, &size));
  guint8 *data = g_malloc(size);
  g_assert_true(cmsSaveProfileToMem(profile, data, &size));
  cmsCloseProfile(profile);
  return g_bytes_new_take(data, size);
}

static void
write_jpeg_icc_marker(struct jpeg_compress_struct *cinfo, GBytes *profile)
{
  gsize profile_size = 0;
  const guint8 *profile_data = g_bytes_get_data(profile, &profile_size);
  g_assert_cmpuint(profile_size, <, 65519);
  guint8 *icc_marker = g_malloc(profile_size + 14);
  memcpy(icc_marker, "ICC_PROFILE\0", 12);
  icc_marker[12] = 1;
  icc_marker[13] = 1;
  memcpy(icc_marker + 14, profile_data, profile_size);
  jpeg_write_marker(cinfo,
                    JPEG_APP0 + 2,
                    icc_marker,
                    profile_size + 14);
  g_free(icc_marker);
}

static void
fill_exif_orientation(guint8 data[32], guint orientation)
{
  const guint8 prefix[] = {'E', 'x', 'i', 'f', 0, 0};
  memcpy(data, prefix, sizeof(prefix));
  guint8 *tiff = data + sizeof(prefix);
  tiff[0] = 'I';
  tiff[1] = 'I';
  tiff[2] = 42;
  tiff[4] = 8;
  tiff[8] = 1;
  tiff[10] = 0x12;
  tiff[11] = 0x01;
  tiff[12] = 3;
  tiff[14] = 1;
  tiff[18] = orientation;
}

static void
write_test_jpeg(const gchar *path, GBytes *profile, guint orientation)
{
  FILE *output = g_fopen(path, "wb");
  g_assert_nonnull(output);

  struct jpeg_compress_struct cinfo = {0};
  struct jpeg_error_mgr error = {0};
  cinfo.err = jpeg_std_error(&error);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, output);
  cinfo.image_width = 24;
  cinfo.image_height = 16;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  for (guint i = 0; i < 3; i++) {
    cinfo.comp_info[i].h_samp_factor = 1;
    cinfo.comp_info[i].v_samp_factor = 1;
  }
  jpeg_set_quality(&cinfo, 92, TRUE);
  jpeg_start_compress(&cinfo, TRUE);

  write_jpeg_icc_marker(&cinfo, profile);

  guint8 exif[32] = {0};
  fill_exif_orientation(exif, orientation);
  jpeg_write_marker(&cinfo, JPEG_APP0 + 1, exif, sizeof(exif));

  guint8 row[24 * 3];
  while (cinfo.next_scanline < cinfo.image_height) {
    for (guint x = 0; x < 24; x++) {
      row[x * 3] = (guint8)(x * 10);
      row[x * 3 + 1] = (guint8)(cinfo.next_scanline * 14);
      row[x * 3 + 2] = (guint8)(255 - x * 8);
    }
    JSAMPROW rows[] = {row};
    jpeg_write_scanlines(&cinfo, rows, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  fclose(output);
}

static void
write_test_gray_jpeg(const gchar *path, GBytes *profile)
{
  FILE *output = g_fopen(path, "wb");
  g_assert_nonnull(output);

  struct jpeg_compress_struct cinfo = {0};
  struct jpeg_error_mgr error = {0};
  cinfo.err = jpeg_std_error(&error);
  jpeg_create_compress(&cinfo);
  jpeg_stdio_dest(&cinfo, output);
  cinfo.image_width = 8;
  cinfo.image_height = 8;
  cinfo.input_components = 1;
  cinfo.in_color_space = JCS_GRAYSCALE;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, 92, TRUE);
  jpeg_start_compress(&cinfo, TRUE);
  write_jpeg_icc_marker(&cinfo, profile);

  guint8 row[8];
  while (cinfo.next_scanline < cinfo.image_height) {
    for (guint x = 0; x < 8; x++)
      row[x] = (guint8)(x * 24 + cinfo.next_scanline * 8);
    JSAMPROW rows[] = {row};
    jpeg_write_scanlines(&cinfo, rows, 1);
  }

  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  fclose(output);
}

static void
write_test_png(const gchar *path, GBytes *profile)
{
  FILE *output = g_fopen(path, "wb");
  g_assert_nonnull(output);

  png_structp png =
    png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  g_assert_nonnull(png);
  png_infop info = png_create_info_struct(png);
  g_assert_nonnull(info);
  g_assert_cmpint(setjmp(png_jmpbuf(png)), ==, 0);

  png_init_io(png, output);
  png_set_IHDR(png,
               info,
               3,
               2,
               8,
               PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);
  gsize profile_size = 0;
  const guint8 *profile_data = g_bytes_get_data(profile, &profile_size);
  png_set_iCCP(png,
               info,
               "sRGB",
               PNG_COMPRESSION_TYPE_BASE,
               profile_data,
               profile_size);
  png_write_info(png, info);

  guint8 pixels[] = {
    255, 0, 0, 255, 0, 255, 0, 192, 0, 0, 255, 128,
    255, 255, 255, 64, 128, 128, 128, 32, 0, 0, 0, 0,
  };
  png_bytep rows[] = {pixels, pixels + 12};
  png_write_image(png, rows);
  png_write_end(png, NULL);
  png_destroy_write_struct(&png, &info);
  fclose(output);
}

static LoslesImage *
load_path(LoslesFormatRegistry *registry, const gchar *path)
{
  g_autoptr(GFile) file = g_file_new_for_path(path);
  g_autoptr(GError) error = NULL;
  LoslesImage *image =
    losles_format_registry_load(registry, file, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(image);
  return image;
}

static void
test_embedded_profiles_and_render(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *directory =
    g_dir_make_tmp("losles-formats-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree gchar *jpeg_path =
    g_build_filename(directory, "profiled.jpg", NULL);
  g_autofree gchar *png_path =
    g_build_filename(directory, "profiled.png", NULL);
  g_autofree gchar *gray_jpeg_path =
    g_build_filename(directory, "gray-profiled.jpg", NULL);
  g_autoptr(GBytes) profile = make_srgb_profile();
  g_autoptr(GBytes) gray_profile = make_gray_profile();
  write_test_jpeg(jpeg_path, profile, 6);
  write_test_png(png_path, profile);
  write_test_gray_jpeg(gray_jpeg_path, gray_profile);

  g_autoptr(LoslesFormatRegistry) registry =
    losles_format_registry_new();
  g_autoptr(LoslesImage) jpeg = load_path(registry, jpeg_path);
  g_assert_cmpuint(losles_image_get_width(jpeg), ==, 24);
  g_assert_cmpuint(losles_image_get_height(jpeg), ==, 16);
  g_assert_cmpuint(losles_image_get_display_width(jpeg), ==, 16);
  g_assert_cmpuint(losles_image_get_display_height(jpeg), ==, 24);
  g_assert_cmpuint(losles_image_get_orientation(jpeg), ==, 6);
  g_assert_nonnull(losles_image_get_icc_profile(jpeg));

  g_autoptr(LoslesColorTarget) target =
    losles_color_target_new_for_profile(profile,
                                        "Test sRGB monitor",
                                        "test-srgb",
                                        &error);
  g_assert_no_error(error);
  g_assert_nonnull(target);
  g_autoptr(LoslesRenderedImage) jpeg_rendered =
    losles_color_target_render(target, jpeg, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(jpeg_rendered);
  g_assert_cmpuint(jpeg_rendered->width, ==, 16);
  g_assert_cmpuint(jpeg_rendered->height, ==, 24);
  g_assert_cmpstr(jpeg_rendered->display_profile_id, ==, "test-srgb");
  g_assert_true(jpeg_rendered->used_embedded_profile);

  g_autoptr(LoslesImage) png = load_path(registry, png_path);
  g_assert_cmpuint(losles_image_get_width(png), ==, 3);
  g_assert_cmpuint(losles_image_get_height(png), ==, 2);
  g_assert_cmpint(losles_image_get_pixel_format(png),
                  ==,
                  LOSLES_PIXEL_FORMAT_RGBA8);
  g_assert_nonnull(losles_image_get_icc_profile(png));
  g_autoptr(LoslesRenderedImage) png_rendered =
    losles_color_target_render(target, png, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(png_rendered);
  g_assert_cmpint(png_rendered->pixel_format,
                  ==,
                  LOSLES_PIXEL_FORMAT_RGBA8);
  g_assert_true(png_rendered->used_embedded_profile);
  gsize rendered_size = 0;
  const guint8 *rendered_pixels =
    g_bytes_get_data(png_rendered->pixels, &rendered_size);
  g_assert_cmpuint(rendered_size, ==, 3 * 2 * 4);
  g_assert_cmpuint(rendered_pixels[3], ==, 255);
  g_assert_cmpuint(rendered_pixels[7], ==, 192);
  g_assert_cmpuint(rendered_pixels[23], ==, 0);

  g_autoptr(LoslesImage) gray_jpeg =
    load_path(registry, gray_jpeg_path);
  g_assert_cmpint(losles_image_get_pixel_format(gray_jpeg),
                  ==,
                  LOSLES_PIXEL_FORMAT_G8);
  g_assert_true(g_bytes_equal(losles_image_get_icc_profile(gray_jpeg),
                              gray_profile));
  g_autoptr(LoslesRenderedImage) gray_rendered =
    losles_color_target_render(target, gray_jpeg, NULL, &error);
  g_assert_no_error(error);
  g_assert_nonnull(gray_rendered);
  g_assert_cmpint(gray_rendered->pixel_format,
                  ==,
                  LOSLES_PIXEL_FORMAT_RGB8);
  g_assert_true(gray_rendered->used_embedded_profile);

  g_assert_cmpint(g_remove(jpeg_path), ==, 0);
  g_assert_cmpint(g_remove(png_path), ==, 0);
  g_assert_cmpint(g_remove(gray_jpeg_path), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void
test_lossless_jpeg_operations(void)
{
  g_autofree gchar *jpegtran = g_find_program_in_path("jpegtran");
  if (!jpegtran) {
    g_test_skip("jpegtran is not installed");
    return;
  }

  g_autoptr(GError) error = NULL;
  g_autofree gchar *directory =
    g_dir_make_tmp("losles-lossless-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree gchar *oriented_path =
    g_build_filename(directory, "oriented.jpg", NULL);
  g_autofree gchar *plain_path =
    g_build_filename(directory, "plain.jpg", NULL);
  g_autofree gchar *rotated_path =
    g_build_filename(directory, "rotated.jpg", NULL);
  g_autofree gchar *cropped_path =
    g_build_filename(directory, "cropped.jpg", NULL);
  g_autoptr(GBytes) profile = make_srgb_profile();
  write_test_jpeg(oriented_path, profile, 6);
  write_test_jpeg(plain_path, profile, 1);

  g_autoptr(LoslesFormatRegistry) registry =
    losles_format_registry_new();
  g_autoptr(LoslesImage) oriented = load_path(registry, oriented_path);
  LoslesFormat *format =
    LOSLES_FORMAT(losles_image_get_format(oriented));
  g_autoptr(GFile) rotated_file = g_file_new_for_path(rotated_path);
  g_assert_true(losles_format_rotate_lossless(format,
                                              oriented,
                                              rotated_file,
                                              LOSLES_ROTATE_RIGHT,
                                              NULL,
                                              &error));
  g_assert_no_error(error);
  g_autoptr(LoslesImage) rotated = load_path(registry, rotated_path);
  g_assert_cmpuint(losles_image_get_width(rotated), ==, 24);
  g_assert_cmpuint(losles_image_get_height(rotated), ==, 16);
  g_assert_cmpuint(losles_image_get_orientation(rotated), ==, 1);
  g_assert_true(g_bytes_equal(losles_image_get_icc_profile(rotated),
                              profile));

  g_autoptr(LoslesImage) plain = load_path(registry, plain_path);
  format = LOSLES_FORMAT(losles_image_get_format(plain));
  LoslesCrop requested = {.x = 9, .y = 1, .width = 5, .height = 5};
  LoslesCrop adjusted = {0};
  g_assert_true(losles_format_adjust_crop(format,
                                          plain,
                                          &requested,
                                          &adjusted,
                                          &error));
  g_assert_no_error(error);
  g_assert_cmpuint(adjusted.x, ==, 8);
  g_assert_cmpuint(adjusted.y, ==, 0);
  g_assert_cmpuint(adjusted.width, ==, 8);
  g_assert_cmpuint(adjusted.height, ==, 8);

  g_autoptr(GFile) cropped_file = g_file_new_for_path(cropped_path);
  g_assert_true(losles_format_crop_lossless(format,
                                            plain,
                                            cropped_file,
                                            &adjusted,
                                            NULL,
                                            &error));
  g_assert_no_error(error);
  g_autoptr(LoslesImage) cropped = load_path(registry, cropped_path);
  g_assert_cmpuint(losles_image_get_width(cropped), ==, 8);
  g_assert_cmpuint(losles_image_get_height(cropped), ==, 8);
  g_assert_true(g_bytes_equal(losles_image_get_icc_profile(cropped),
                              profile));

  g_assert_cmpint(g_remove(oriented_path), ==, 0);
  g_assert_cmpint(g_remove(plain_path), ==, 0);
  g_assert_cmpint(g_remove(rotated_path), ==, 0);
  g_assert_cmpint(g_remove(cropped_path), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

static void
test_invalid_inputs(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *directory =
    g_dir_make_tmp("losles-invalid-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree gchar *jpeg_path =
    g_build_filename(directory, "broken.jpg", NULL);
  g_autofree gchar *png_path =
    g_build_filename(directory, "broken.png", NULL);
  const guint8 jpeg_data[] = {
    0xff, 0xd8, 0xff, 0xe0, 0x00, 0x01,
  };
  const guint8 png_data[] = {
    0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0,
  };
  g_assert_true(g_file_set_contents(jpeg_path,
                                    (const gchar *)jpeg_data,
                                    sizeof(jpeg_data),
                                    &error));
  g_assert_no_error(error);
  g_assert_true(g_file_set_contents(png_path,
                                    (const gchar *)png_data,
                                    sizeof(png_data),
                                    &error));
  g_assert_no_error(error);

  g_autoptr(LoslesFormatRegistry) registry =
    losles_format_registry_new();
  g_autoptr(GFile) jpeg_file = g_file_new_for_path(jpeg_path);
  g_autoptr(LoslesImage) jpeg =
    losles_format_registry_load(registry, jpeg_file, NULL, &error);
  g_assert_null(jpeg);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&error);

  g_autoptr(GFile) png_file = g_file_new_for_path(png_path);
  g_autoptr(LoslesImage) png =
    losles_format_registry_load(registry, png_file, NULL, &error);
  g_assert_null(png);
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&error);

  g_assert_cmpint(g_remove(jpeg_path), ==, 0);
  g_assert_cmpint(g_remove(png_path), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/formats/embedded-profiles-and-render",
                  test_embedded_profiles_and_render);
  g_test_add_func("/formats/lossless-jpeg-operations",
                  test_lossless_jpeg_operations);
  g_test_add_func("/formats/invalid-inputs", test_invalid_inputs);
  return g_test_run();
}
