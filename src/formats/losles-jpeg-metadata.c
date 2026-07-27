#include "losles-jpeg-metadata.h"

typedef enum {
  TIFF_ENDIAN_LITTLE,
  TIFF_ENDIAN_BIG,
} TiffEndian;

static gboolean
read_u16(const guint8 *data,
         gsize size,
         gsize offset,
         TiffEndian endian,
         guint16 *value)
{
  if (offset > size || size - offset < 2)
    return FALSE;

  if (endian == TIFF_ENDIAN_LITTLE)
    *value = (guint16)data[offset] | ((guint16)data[offset + 1] << 8);
  else
    *value = ((guint16)data[offset] << 8) | (guint16)data[offset + 1];
  return TRUE;
}

static gboolean
read_u32(const guint8 *data,
         gsize size,
         gsize offset,
         TiffEndian endian,
         guint32 *value)
{
  if (offset > size || size - offset < 4)
    return FALSE;

  if (endian == TIFF_ENDIAN_LITTLE) {
    *value = (guint32)data[offset] |
             ((guint32)data[offset + 1] << 8) |
             ((guint32)data[offset + 2] << 16) |
             ((guint32)data[offset + 3] << 24);
  } else {
    *value = ((guint32)data[offset] << 24) |
             ((guint32)data[offset + 1] << 16) |
             ((guint32)data[offset + 2] << 8) |
             (guint32)data[offset + 3];
  }
  return TRUE;
}

static gboolean
find_orientation(guint8 *data,
                 gsize size,
                 guint *orientation,
                 gsize *value_offset,
                 TiffEndian *value_endian)
{
  if (size < 4 || data[0] != 0xff || data[1] != 0xd8)
    return FALSE;

  gsize position = 2;
  while (position + 4 <= size) {
    if (data[position] != 0xff)
      return FALSE;

    while (position < size && data[position] == 0xff)
      position++;
    if (position >= size)
      return FALSE;

    const guint8 marker = data[position++];
    if (marker == 0xd9 || marker == 0xda)
      break;
    if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd8))
      continue;

    if (position + 2 > size)
      return FALSE;
    const guint16 marker_length =
      ((guint16)data[position] << 8) | data[position + 1];
    if (marker_length < 2 || position + marker_length > size)
      return FALSE;

    guint8 *segment = data + position + 2;
    const gsize segment_size = marker_length - 2;
    position += marker_length;

    if (marker != 0xe1 ||
        segment_size < 14 ||
        memcmp(segment, "Exif\0\0", 6) != 0)
      continue;

    guint8 *tiff = segment + 6;
    const gsize tiff_size = segment_size - 6;
    TiffEndian endian;
    if (tiff[0] == 'I' && tiff[1] == 'I')
      endian = TIFF_ENDIAN_LITTLE;
    else if (tiff[0] == 'M' && tiff[1] == 'M')
      endian = TIFF_ENDIAN_BIG;
    else
      continue;

    guint16 magic = 0;
    guint32 ifd_offset = 0;
    if (!read_u16(tiff, tiff_size, 2, endian, &magic) ||
        magic != 42 ||
        !read_u32(tiff, tiff_size, 4, endian, &ifd_offset))
      continue;

    guint16 count = 0;
    if (!read_u16(tiff, tiff_size, ifd_offset, endian, &count))
      continue;

    for (guint i = 0; i < count; i++) {
      const gsize entry = (gsize)ifd_offset + 2 + (gsize)i * 12;
      guint16 tag = 0;
      guint16 type = 0;
      guint32 components = 0;
      guint16 candidate = 0;
      if (!read_u16(tiff, tiff_size, entry, endian, &tag) ||
          !read_u16(tiff, tiff_size, entry + 2, endian, &type) ||
          !read_u32(tiff, tiff_size, entry + 4, endian, &components))
        break;

      if (tag != 0x0112 || type != 3 || components != 1)
        continue;
      if (!read_u16(tiff, tiff_size, entry + 8, endian, &candidate))
        continue;
      if (candidate < 1 || candidate > 8)
        return FALSE;

      if (orientation)
        *orientation = candidate;
      if (value_offset)
        *value_offset = (gsize)(tiff - data) + entry + 8;
      if (value_endian)
        *value_endian = endian;
      return TRUE;
    }
  }

  return FALSE;
}

guint
losles_jpeg_metadata_get_orientation(GBytes *encoded)
{
  g_return_val_if_fail(encoded != NULL, 1);

  gsize size = 0;
  const guint8 *data = g_bytes_get_data(encoded, &size);
  guint orientation = 1;
  find_orientation((guint8 *)data, size, &orientation, NULL, NULL);
  return orientation;
}

gboolean
losles_jpeg_metadata_set_orientation_in_file(const gchar *path,
                                              guint orientation,
                                              GError **error)
{
  g_return_val_if_fail(path != NULL, FALSE);
  g_return_val_if_fail(orientation >= 1 && orientation <= 8, FALSE);

  g_autofree gchar *contents = NULL;
  gsize size = 0;
  if (!g_file_get_contents(path, &contents, &size, error))
    return FALSE;

  gsize offset = 0;
  TiffEndian endian = TIFF_ENDIAN_LITTLE;
  if (!find_orientation((guint8 *)contents,
                        size,
                        NULL,
                        &offset,
                        &endian))
    return TRUE;

  if (endian == TIFF_ENDIAN_LITTLE) {
    contents[offset] = (guint8)orientation;
    contents[offset + 1] = 0;
  } else {
    contents[offset] = 0;
    contents[offset + 1] = (guint8)orientation;
  }

  return g_file_set_contents(path, contents, size, error);
}
