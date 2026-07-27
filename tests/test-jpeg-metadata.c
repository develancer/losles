#include <glib.h>
#include <glib/gstdio.h>

#include "../src/formats/losles-jpeg-metadata.h"

static GBytes *
minimal_exif_jpeg(gboolean little_endian, guint orientation)
{
  static const guint8 prefix[] = {
    0xff, 0xd8,
    0xff, 0xe1, 0x00, 0x22,
    'E', 'x', 'i', 'f', 0x00, 0x00,
  };
  guint8 data[40] = {0};
  memcpy(data, prefix, sizeof(prefix));

  guint8 *tiff = data + sizeof(prefix);
  if (little_endian) {
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
  } else {
    tiff[0] = 'M';
    tiff[1] = 'M';
    tiff[3] = 42;
    tiff[7] = 8;
    tiff[9] = 1;
    tiff[10] = 0x01;
    tiff[11] = 0x12;
    tiff[13] = 3;
    tiff[17] = 1;
    tiff[19] = orientation;
  }

  data[38] = 0xff;
  data[39] = 0xd9;
  return g_bytes_new(data, sizeof(data));
}

static void
test_orientation_little_endian(void)
{
  g_autoptr(GBytes) jpeg = minimal_exif_jpeg(TRUE, 6);
  guint orientation = 1;
  g_assert_true(
    losles_jpeg_metadata_read_orientation(jpeg, &orientation));
  g_assert_cmpuint(orientation, ==, 6);
  g_assert_cmpuint(losles_jpeg_metadata_get_orientation(jpeg), ==, 6);
}

static void
test_orientation_big_endian(void)
{
  g_autoptr(GBytes) jpeg = minimal_exif_jpeg(FALSE, 8);
  guint orientation = 1;
  g_assert_true(
    losles_jpeg_metadata_read_orientation(jpeg, &orientation));
  g_assert_cmpuint(orientation, ==, 8);
  g_assert_cmpuint(losles_jpeg_metadata_get_orientation(jpeg), ==, 8);
}

static void
test_orientation_one_is_present(void)
{
  g_autoptr(GBytes) jpeg = minimal_exif_jpeg(TRUE, 1);
  guint orientation = 8;
  g_assert_true(
    losles_jpeg_metadata_read_orientation(jpeg, &orientation));
  g_assert_cmpuint(orientation, ==, 1);
}

static void
test_no_exif(void)
{
  const guint8 data[] = {0xff, 0xd8, 0xff, 0xd9};
  g_autoptr(GBytes) jpeg = g_bytes_new_static(data, sizeof(data));
  guint orientation = 8;
  g_assert_false(
    losles_jpeg_metadata_read_orientation(jpeg, &orientation));
  g_assert_cmpuint(orientation, ==, 1);
  g_assert_cmpuint(losles_jpeg_metadata_get_orientation(jpeg), ==, 1);
}

static void
test_orientation_writer(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *directory =
    g_dir_make_tmp("losles-jpeg-metadata-XXXXXX", &error);
  g_assert_no_error(error);
  g_autofree gchar *path =
    g_build_filename(directory, "orientation.jpg", NULL);

  for (guint little_endian = 0; little_endian <= 1; little_endian++) {
    g_autoptr(GBytes) jpeg =
      minimal_exif_jpeg(little_endian, little_endian ? 6 : 8);
    gsize size = 0;
    const gchar *data = g_bytes_get_data(jpeg, &size);
    g_assert_true(g_file_set_contents(path, data, size, &error));
    g_assert_no_error(error);
    g_assert_true(
      losles_jpeg_metadata_set_orientation_in_file(path, 1, &error));
    g_assert_no_error(error);

    g_autofree gchar *normalized_data = NULL;
    gsize normalized_size = 0;
    g_assert_true(g_file_get_contents(path,
                                      &normalized_data,
                                      &normalized_size,
                                      &error));
    g_assert_no_error(error);
    g_autoptr(GBytes) normalized =
      g_bytes_new_take(g_steal_pointer(&normalized_data),
                       normalized_size);
    guint orientation = 8;
    g_assert_true(losles_jpeg_metadata_read_orientation(normalized,
                                                       &orientation));
    g_assert_cmpuint(orientation, ==, 1);
  }

  const guint8 no_exif_data[] = {0xff, 0xd8, 0xff, 0xd9};
  g_assert_true(g_file_set_contents(path,
                                    (const gchar *)no_exif_data,
                                    sizeof(no_exif_data),
                                    &error));
  g_assert_no_error(error);
  g_assert_false(
    losles_jpeg_metadata_set_orientation_in_file(path, 1, &error));
  g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  g_clear_error(&error);

  g_assert_cmpint(g_remove(path), ==, 0);
  g_assert_cmpint(g_rmdir(directory), ==, 0);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/jpeg/orientation/little-endian",
                  test_orientation_little_endian);
  g_test_add_func("/jpeg/orientation/big-endian",
                  test_orientation_big_endian);
  g_test_add_func("/jpeg/orientation/value-one-is-present",
                  test_orientation_one_is_present);
  g_test_add_func("/jpeg/orientation/no-exif", test_no_exif);
  g_test_add_func("/jpeg/orientation/writer", test_orientation_writer);
  return g_test_run();
}
