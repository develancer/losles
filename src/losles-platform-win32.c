#include "losles-platform.h"

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <glib/gwin32.h>

#include "losles-config.h"

static void
set_win32_error(GError **error,
                DWORD code,
                const gchar *operation)
{
  g_autofree gchar *message = g_win32_error_message((gint)code);
  g_set_error(error,
              G_IO_ERROR,
              G_IO_ERROR_FAILED,
              "%s: %s (Windows error %lu)",
              operation,
              message ? message : "unknown error",
              (gulong)code);
}

static void
set_hresult_error(GError **error,
                  HRESULT result,
                  const gchar *operation)
{
  g_autofree gchar *message =
    g_win32_error_message((gint)result);
  g_set_error(error,
              G_IO_ERROR,
              G_IO_ERROR_FAILED,
              "%s: %s (HRESULT 0x%08lx)",
              operation,
              message ? message : "unknown error",
              (gulong)result);
}

gboolean
losles_platform_get_total_memory(guint64 *total_memory, GError **error)
{
  g_return_val_if_fail(total_memory != NULL, FALSE);

  MEMORYSTATUSEX status = {
    .dwLength = sizeof(status),
  };
  if (!GlobalMemoryStatusEx(&status)) {
    set_win32_error(error,
                    GetLastError(),
                    "Could not determine total system memory");
    return FALSE;
  }
  if (status.ullTotalPhys == 0) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_FAILED,
                        "Windows reported zero bytes of physical memory");
    return FALSE;
  }

  *total_memory = status.ullTotalPhys;
  return TRUE;
}

gboolean
losles_platform_trash_file(GFile *file,
                           GCancellable *cancellable,
                           GError **error)
{
  g_return_val_if_fail(G_IS_FILE(file), FALSE);

  if (cancellable &&
      g_cancellable_set_error_if_cancelled(cancellable, error))
    return FALSE;

  g_autofree gchar *path = g_file_get_path(file);
  if (!path) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "The Windows Recycle Bin requires a local file");
    return FALSE;
  }

  g_autofree gunichar2 *wide_path =
    g_utf8_to_utf16(path, -1, NULL, NULL, error);
  if (!wide_path)
    return FALSE;

  const HRESULT initialize_result =
    CoInitializeEx(NULL,
                   COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const gboolean should_uninitialize = SUCCEEDED(initialize_result);
  if (FAILED(initialize_result) &&
      initialize_result != RPC_E_CHANGED_MODE) {
    set_hresult_error(error,
                      initialize_result,
                      "Could not initialize the Windows shell");
    return FALSE;
  }

  IFileOperation *operation = NULL;
  IShellItem *item = NULL;
  HRESULT result =
    CoCreateInstance(&CLSID_FileOperation,
                     NULL,
                     CLSCTX_INPROC_SERVER,
                     &IID_IFileOperation,
                     (void **)&operation);
  if (SUCCEEDED(result)) {
    result =
      IFileOperation_SetOperationFlags(
        operation,
        FOF_ALLOWUNDO |
          FOF_NOCONFIRMATION |
          FOF_SILENT |
          FOF_NOERRORUI |
          FOFX_RECYCLEONDELETE);
  }
  if (SUCCEEDED(result)) {
    result =
      SHCreateItemFromParsingName((PCWSTR)wide_path,
                                  NULL,
                                  &IID_IShellItem,
                                  (void **)&item);
  }
  if (SUCCEEDED(result))
    result = IFileOperation_DeleteItem(operation, item, NULL);
  if (SUCCEEDED(result))
    result = IFileOperation_PerformOperations(operation);

  BOOL aborted = FALSE;
  if (SUCCEEDED(result)) {
    result =
      IFileOperation_GetAnyOperationsAborted(operation, &aborted);
    if (SUCCEEDED(result) && aborted)
      result = HRESULT_FROM_WIN32(ERROR_CANCELLED);
  }

  if (item)
    IShellItem_Release(item);
  if (operation)
    IFileOperation_Release(operation);
  if (should_uninitialize)
    CoUninitialize();

  if (FAILED(result)) {
    set_hresult_error(error,
                      result,
                      "Could not move the file to the Recycle Bin");
    return FALSE;
  }
  return TRUE;
}

gboolean
losles_platform_create_hard_link(const gchar *source_path,
                                 const gchar *destination_path,
                                 GError **error)
{
  g_autofree gunichar2 *wide_source =
    g_utf8_to_utf16(source_path, -1, NULL, NULL, error);
  if (!wide_source)
    return FALSE;
  g_autofree gunichar2 *wide_destination =
    g_utf8_to_utf16(destination_path, -1, NULL, NULL, error);
  if (!wide_destination)
    return FALSE;

  if (CreateHardLinkW((PCWSTR)wide_destination,
                      (PCWSTR)wide_source,
                      NULL))
    return TRUE;

  set_win32_error(error,
                  GetLastError(),
                  "Could not create the crop safety hard link");
  return FALSE;
}

void
losles_platform_copy_file_permissions(const gchar *source_path,
                                      const gchar *destination_path)
{
  /*
   * The transformed file is created beside the source, so it inherits the
   * same directory ACL. Windows file attributes are intentionally not copied
   * before replacement because a read-only attribute would prevent commit.
   */
  (void)source_path;
  (void)destination_path;
}

gchar *
losles_platform_get_portable_icon_path(void)
{
  DWORD capacity = MAX_PATH;
  g_autofree gunichar2 *module_path = NULL;

  for (;;) {
    module_path = g_realloc(module_path,
                            (gsize)capacity * sizeof(*module_path));
    const DWORD length =
      GetModuleFileNameW(NULL, (LPWSTR)module_path, capacity);
    if (length == 0)
      return NULL;
    if (length < capacity - 1)
      break;
    if (capacity > 32768)
      return NULL;
    capacity *= 2;
  }

  g_autofree gchar *utf8_path =
    g_utf16_to_utf8(module_path, -1, NULL, NULL, NULL);
  if (!utf8_path)
    return NULL;

  g_autofree gchar *binary_directory = g_path_get_dirname(utf8_path);
  g_autofree gchar *bundle_root = g_path_get_dirname(binary_directory);
  g_autofree gchar *source_icon =
    g_build_filename(bundle_root,
                     "data",
                     "icons",
                     "hicolor",
                     "512x512",
                     "apps",
                     LOSLES_APPLICATION_ID ".png",
                     NULL);
  if (g_file_test(source_icon, G_FILE_TEST_IS_REGULAR))
    return g_steal_pointer(&source_icon);

  return g_build_filename(bundle_root,
                          "share",
                          "icons",
                          "hicolor",
                          "512x512",
                          "apps",
                          LOSLES_APPLICATION_ID ".png",
                          NULL);
}
