#include "losles-platform.h"

#include <errno.h>
#include <glib/gstdio.h>
#include <sys/sysinfo.h>
#include <unistd.h>

gboolean
losles_platform_get_total_memory(guint64 *total_memory, GError **error)
{
  g_return_val_if_fail(total_memory != NULL, FALSE);

  struct sysinfo info = {0};
  if (sysinfo(&info) != 0) {
    const gint saved_errno = errno;
    g_set_error(error,
                G_IO_ERROR,
                g_io_error_from_errno(saved_errno),
                "Could not determine total system memory: %s",
                g_strerror(saved_errno));
    return FALSE;
  }

  guint64 bytes = info.totalram;
  if (info.mem_unit > 0 && bytes > G_MAXUINT64 / info.mem_unit)
    bytes = G_MAXUINT64;
  else
    bytes *= info.mem_unit;

  if (bytes == 0) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_FAILED,
                        "The system reported zero bytes of memory");
    return FALSE;
  }

  *total_memory = bytes;
  return TRUE;
}

gboolean
losles_platform_trash_file(GFile *file,
                           GCancellable *cancellable,
                           GError **error)
{
  return g_file_trash(file, cancellable, error);
}

gboolean
losles_platform_create_hard_link(const gchar *source_path,
                                 const gchar *destination_path,
                                 GError **error)
{
  if (link(source_path, destination_path) == 0)
    return TRUE;

  const gint saved_errno = errno;
  g_set_error(error,
              G_IO_ERROR,
              g_io_error_from_errno(saved_errno),
              "Could not create a hard link: %s",
              g_strerror(saved_errno));
  return FALSE;
}

void
losles_platform_copy_file_permissions(const gchar *source_path,
                                      const gchar *destination_path)
{
  GStatBuf source_stat;
  if (g_stat(source_path, &source_stat) == 0 &&
      g_chmod(destination_path, source_stat.st_mode & 07777) != 0) {
    g_debug("Could not preserve file permissions on %s: %s",
            destination_path,
            g_strerror(errno));
  }
}

gchar *
losles_platform_get_portable_icon_path(void)
{
  return NULL;
}
