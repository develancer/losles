#include <glib.h>

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
  g_assert_cmpuint(losles_jpeg_metadata_get_orientation(jpeg), ==, 6);
}

static void
test_orientation_big_endian(void)
{
  g_autoptr(GBytes) jpeg = minimal_exif_jpeg(FALSE, 8);
  g_assert_cmpuint(losles_jpeg_metadata_get_orientation(jpeg), ==, 8);
}

static void
test_no_exif(void)
{
  const guint8 data[] = {0xff, 0xd8, 0xff, 0xd9};
  g_autoptr(GBytes) jpeg = g_bytes_new_static(data, sizeof(data));
  g_assert_cmpuint(losles_jpeg_metadata_get_orientation(jpeg), ==, 1);
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/jpeg/orientation/little-endian",
                  test_orientation_little_endian);
  g_test_add_func("/jpeg/orientation/big-endian",
                  test_orientation_big_endian);
  g_test_add_func("/jpeg/orientation/no-exif", test_no_exif);
  return g_test_run();
}
