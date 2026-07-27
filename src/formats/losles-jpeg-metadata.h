#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean losles_jpeg_metadata_read_orientation(GBytes *encoded,
                                               guint *orientation);
guint losles_jpeg_metadata_get_orientation(GBytes *encoded);
gboolean losles_jpeg_metadata_set_orientation_in_file(const gchar *path,
                                                       guint orientation,
                                                       GError **error);

G_END_DECLS
