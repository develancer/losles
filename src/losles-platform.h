#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean losles_platform_get_total_memory(guint64 *total_memory,
                                          GError **error);
gboolean losles_platform_trash_file(GFile *file,
                                    GCancellable *cancellable,
                                    GError **error);
gboolean losles_platform_create_hard_link(const gchar *source_path,
                                          const gchar *destination_path,
                                          GError **error);
void losles_platform_copy_file_permissions(const gchar *source_path,
                                           const gchar *destination_path);
gchar *losles_platform_get_portable_icon_path(void);

G_END_DECLS
