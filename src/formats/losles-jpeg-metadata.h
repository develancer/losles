#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

guint losles_jpeg_metadata_get_orientation(GBytes *encoded);
gboolean losles_jpeg_metadata_set_orientation_in_file(const gchar *path,
                                                       guint orientation,
                                                       GError **error);

G_END_DECLS
