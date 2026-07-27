#include "losles-color-manager.h"

#include <lcms2.h>
#include <string.h>

struct _LoslesColorTarget {
  gint ref_count;
  GBytes *profile;
  gchar *name;
  gchar *id;
  gboolean fallback;
};

struct _LoslesColorManager {
  GObject parent_instance;

  CdClient *client;
  gboolean connected;
  gchar *active_connector;
  CdDevice *active_device;
  gulong active_device_changed_id;
  LoslesColorTarget *cached_target;
};

enum {
  COLOR_TARGET_CHANGED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE(LoslesColorManager,
                    losles_color_manager,
                    G_TYPE_OBJECT)

static GBytes *
profile_to_bytes(cmsHPROFILE profile)
{
  cmsUInt32Number size = 0;
  if (!cmsSaveProfileToMem(profile, NULL, &size) || size == 0)
    return NULL;

  guint8 *data = g_malloc(size);
  if (!cmsSaveProfileToMem(profile, data, &size)) {
    g_free(data);
    return NULL;
  }
  return g_bytes_new_take(data, size);
}

static LoslesColorTarget *
color_target_new(GBytes *profile,
                 const gchar *name,
                 const gchar *id,
                 gboolean fallback)
{
  LoslesColorTarget *self = g_new0(LoslesColorTarget, 1);
  self->ref_count = 1;
  self->profile = g_bytes_ref(profile);
  self->name = g_strdup(name);
  self->id = g_strdup(id);
  self->fallback = fallback;
  return self;
}

LoslesColorTarget *
losles_color_target_new_for_profile(GBytes *profile,
                                    const gchar *name,
                                    const gchar *id,
                                    GError **error)
{
  g_return_val_if_fail(profile != NULL, NULL);

  gsize size = 0;
  const guint8 *data = g_bytes_get_data(profile, &size);
  cmsHPROFILE lcms_profile = cmsOpenProfileFromMem(data, size);
  if (!lcms_profile) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_DATA,
                        "The display ICC profile is invalid");
    return NULL;
  }

  const gboolean is_rgb =
    cmsGetColorSpace(lcms_profile) == cmsSigRgbData;
  cmsCloseProfile(lcms_profile);
  if (!is_rgb) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NOT_SUPPORTED,
                        "The display ICC profile is not an RGB profile");
    return NULL;
  }

  return color_target_new(profile,
                          name ? name : "Display profile",
                          id ? id : "display-profile",
                          FALSE);
}

LoslesColorTarget *
losles_color_target_ref(LoslesColorTarget *self)
{
  g_return_val_if_fail(self != NULL, NULL);
  g_atomic_int_inc(&self->ref_count);
  return self;
}

void
losles_color_target_unref(LoslesColorTarget *self)
{
  if (!self || !g_atomic_int_dec_and_test(&self->ref_count))
    return;

  g_clear_pointer(&self->profile, g_bytes_unref);
  g_clear_pointer(&self->name, g_free);
  g_clear_pointer(&self->id, g_free);
  g_free(self);
}

const gchar *
losles_color_target_get_name(LoslesColorTarget *self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->name;
}

const gchar *
losles_color_target_get_id(LoslesColorTarget *self)
{
  g_return_val_if_fail(self != NULL, NULL);
  return self->id;
}

gboolean
losles_color_target_is_fallback(LoslesColorTarget *self)
{
  g_return_val_if_fail(self != NULL, TRUE);
  return self->fallback;
}

static LoslesColorTarget *
create_srgb_target(void)
{
  cmsHPROFILE srgb = cmsCreate_sRGBProfile();
  if (!srgb)
    return NULL;
  g_autoptr(GBytes) bytes = profile_to_bytes(srgb);
  cmsCloseProfile(srgb);
  if (!bytes)
    return NULL;
  return color_target_new(bytes, "sRGB fallback", "builtin-srgb", TRUE);
}

static void
clear_active_device(LoslesColorManager *self)
{
  if (self->active_device && self->active_device_changed_id) {
    g_signal_handler_disconnect(self->active_device,
                                self->active_device_changed_id);
  }
  self->active_device_changed_id = 0;
  g_clear_object(&self->active_device);
  g_clear_pointer(&self->active_connector, g_free);
}

static void
active_device_changed(CdDevice *device, LoslesColorManager *self)
{
  (void)device;
  g_clear_pointer(&self->cached_target, losles_color_target_unref);
  g_signal_emit(self, signals[COLOR_TARGET_CHANGED], 0);
}

static CdDevice *
find_device(LoslesColorManager *self, const gchar *connector)
{
  if (!self->connected || !connector) {
    g_debug("Cannot look up connector %s (colord connected: %s)",
            connector ? connector : "(unknown)",
            self->connected ? "yes" : "no");
    return NULL;
  }

  g_autoptr(GError) error = NULL;
  CdDevice *device =
    cd_client_find_device_by_property_sync(self->client,
                                           CD_DEVICE_METADATA_XRANDR_NAME,
                                           connector,
                                           NULL,
                                           &error);
  if (!device) {
    g_debug("No colord device for output %s: %s",
            connector,
            error ? error->message : "not found");
    return NULL;
  }

  g_clear_error(&error);
  if (!cd_device_connect_sync(device, NULL, &error)) {
    g_debug("Could not connect to colord device %s: %s",
            connector,
            error->message);
    g_object_unref(device);
    return NULL;
  }

  return device;
}

static void
ensure_active_device(LoslesColorManager *self, const gchar *connector)
{
  if (g_strcmp0(self->active_connector, connector) == 0 &&
      self->active_device)
    return;

  clear_active_device(self);
  g_clear_pointer(&self->cached_target, losles_color_target_unref);
  self->active_connector = g_strdup(connector);
  self->active_device = find_device(self, connector);
  if (self->active_device) {
    self->active_device_changed_id =
      g_signal_connect(self->active_device,
                       "changed",
                       G_CALLBACK(active_device_changed),
                       self);
  }
}

static LoslesColorTarget *
target_from_active_device(LoslesColorManager *self)
{
  if (!self->active_device) {
    g_debug("No colord device is active for connector %s",
            self->active_connector ? self->active_connector : "(unknown)");
    return NULL;
  }

  CdProfile *profile = cd_device_get_default_profile(self->active_device);
  if (!profile) {
    g_debug("Colord device %s currently has no default profile",
            cd_device_get_id(self->active_device));
    return NULL;
  }

  g_autoptr(GError) error = NULL;
  if (!cd_profile_get_connected(profile) &&
      !cd_profile_connect_sync(profile, NULL, &error)) {
    g_warning("Could not connect to the active display profile: %s",
              error->message);
    return NULL;
  }

  const gchar *filename = cd_profile_get_filename(profile);
  if (!filename) {
    g_debug("Colord default profile %s has no local filename",
            cd_profile_get_id(profile));
    return NULL;
  }

  g_autofree gchar *contents = NULL;
  gsize length = 0;
  if (!g_file_get_contents(filename, &contents, &length, &error)) {
    g_warning("Could not read display ICC profile %s: %s",
              filename,
              error->message);
    return NULL;
  }

  cmsHPROFILE lcms_profile = cmsOpenProfileFromMem(contents, length);
  if (!lcms_profile) {
    g_warning("The active display ICC profile is invalid: %s", filename);
    return NULL;
  }
  const gboolean is_rgb = cmsGetColorSpace(lcms_profile) == cmsSigRgbData;
  cmsCloseProfile(lcms_profile);
  if (!is_rgb) {
    g_warning("The active display profile is not an RGB profile: %s",
              filename);
    return NULL;
  }

  g_autoptr(GBytes) bytes = g_bytes_new_take(g_steal_pointer(&contents),
                                              length);
  const gchar *title = cd_profile_get_title(profile);
  const gchar *id = cd_profile_get_id(profile);
  LoslesColorTarget *target =
    color_target_new(bytes,
                     title && *title ? title : filename,
                     id && *id ? id : filename,
                     FALSE);
  g_debug("Using colord profile “%s” (%s) for connector %s",
          target->name,
          target->id,
          self->active_connector);
  return target;
}

LoslesColorTarget *
losles_color_manager_get_target(LoslesColorManager *self,
                                GdkMonitor *monitor)
{
  g_return_val_if_fail(LOSLES_IS_COLOR_MANAGER(self), NULL);

  const gchar *connector = monitor ? gdk_monitor_get_connector(monitor) : NULL;
  ensure_active_device(self, connector);

  if (!self->cached_target)
    self->cached_target = target_from_active_device(self);
  if (!self->cached_target) {
    self->cached_target = create_srgb_target();
    g_debug("No selected display profile for connector %s; using sRGB",
            connector ? connector : "(unknown)");
  }

  return self->cached_target
           ? losles_color_target_ref(self->cached_target)
           : NULL;
}

static void
losles_color_manager_finalize(GObject *object)
{
  LoslesColorManager *self = LOSLES_COLOR_MANAGER(object);

  clear_active_device(self);
  g_clear_pointer(&self->cached_target, losles_color_target_unref);
  g_clear_object(&self->client);

  G_OBJECT_CLASS(losles_color_manager_parent_class)->finalize(object);
}

static void
losles_color_manager_class_init(LoslesColorManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = losles_color_manager_finalize;

  signals[COLOR_TARGET_CHANGED] =
    g_signal_new("target-changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 NULL,
                 NULL,
                 NULL,
                 G_TYPE_NONE,
                 0);
}

static void
losles_color_manager_init(LoslesColorManager *self)
{
  self->client = cd_client_new();
  g_autoptr(GError) error = NULL;
  self->connected =
    cd_client_connect_sync(self->client, NULL, &error);
  if (!self->connected) {
    g_warning("Could not connect to colord; using sRGB: %s",
              error ? error->message : "unknown error");
  }
}

LoslesColorManager *
losles_color_manager_new(void)
{
  return g_object_new(LOSLES_TYPE_COLOR_MANAGER, NULL);
}

static cmsHPROFILE
create_default_source_profile(LoslesPixelFormat format)
{
  if (format == LOSLES_PIXEL_FORMAT_G8 ||
      format == LOSLES_PIXEL_FORMAT_GA8) {
    cmsToneCurve *gamma = cmsBuildGamma(NULL, 2.2);
    if (!gamma)
      return NULL;
    cmsCIExyY white_point;
    cmsWhitePointFromTemp(&white_point, 6504);
    cmsHPROFILE profile = cmsCreateGrayProfile(&white_point, gamma);
    cmsFreeToneCurve(gamma);
    return profile;
  }
  return cmsCreate_sRGBProfile();
}

static cmsUInt32Number
source_lcms_format(LoslesPixelFormat format)
{
  switch (format) {
  case LOSLES_PIXEL_FORMAT_G8:
    return TYPE_GRAY_8;
  case LOSLES_PIXEL_FORMAT_GA8:
    return TYPE_GRAYA_8;
  case LOSLES_PIXEL_FORMAT_RGB8:
    return TYPE_RGB_8;
  case LOSLES_PIXEL_FORMAT_RGBA8:
    return TYPE_RGBA_8;
  }
  g_assert_not_reached();
}

static gboolean
pixel_format_has_alpha(LoslesPixelFormat format)
{
  return format == LOSLES_PIXEL_FORMAT_GA8 ||
         format == LOSLES_PIXEL_FORMAT_RGBA8;
}

static void
map_oriented_pixel(guint orientation,
                   guint source_width,
                   guint source_height,
                   guint x,
                   guint y,
                   guint *destination_x,
                   guint *destination_y)
{
  switch (orientation) {
  case 2:
    *destination_x = source_width - 1 - x;
    *destination_y = y;
    break;
  case 3:
    *destination_x = source_width - 1 - x;
    *destination_y = source_height - 1 - y;
    break;
  case 4:
    *destination_x = x;
    *destination_y = source_height - 1 - y;
    break;
  case 5:
    *destination_x = y;
    *destination_y = x;
    break;
  case 6:
    *destination_x = source_height - 1 - y;
    *destination_y = x;
    break;
  case 7:
    *destination_x = source_height - 1 - y;
    *destination_y = source_width - 1 - x;
    break;
  case 8:
    *destination_x = y;
    *destination_y = source_width - 1 - x;
    break;
  default:
    *destination_x = x;
    *destination_y = y;
    break;
  }
}

LoslesRenderedImage *
losles_color_target_render(LoslesColorTarget *target,
                           LoslesImage *image,
                           GCancellable *cancellable,
                           GError **error)
{
  g_return_val_if_fail(target != NULL, NULL);
  g_return_val_if_fail(LOSLES_IS_IMAGE(image), NULL);

  const LoslesPixelFormat source_format =
    losles_image_get_pixel_format(image);
  gsize source_profile_size = 0;
  const guint8 *source_profile_data = NULL;
  GBytes *source_profile_bytes = losles_image_get_icc_profile(image);
  cmsHPROFILE source_profile = NULL;
  if (source_profile_bytes) {
    source_profile_data =
      g_bytes_get_data(source_profile_bytes, &source_profile_size);
    source_profile =
      cmsOpenProfileFromMem(source_profile_data, source_profile_size);
  }
  gboolean used_embedded_profile = source_profile != NULL;
  if (!source_profile)
    source_profile = create_default_source_profile(source_format);

  gsize target_profile_size = 0;
  const guint8 *target_profile_data =
    g_bytes_get_data(target->profile, &target_profile_size);
  cmsHPROFILE target_profile =
    cmsOpenProfileFromMem(target_profile_data, target_profile_size);
  if (!source_profile || !target_profile) {
    if (source_profile)
      cmsCloseProfile(source_profile);
    if (target_profile)
      cmsCloseProfile(target_profile);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_INVALID_DATA,
                        "Could not open an ICC color profile");
    return NULL;
  }

  const cmsColorSpaceSignature expected_source_space =
    source_format == LOSLES_PIXEL_FORMAT_G8 ||
        source_format == LOSLES_PIXEL_FORMAT_GA8
      ? cmsSigGrayData
      : cmsSigRgbData;
  if (cmsGetColorSpace(source_profile) != expected_source_space) {
    cmsCloseProfile(source_profile);
    source_profile = create_default_source_profile(source_format);
    used_embedded_profile = FALSE;
  }

  const gboolean alpha = pixel_format_has_alpha(source_format);
  const cmsUInt32Number output_format = alpha ? TYPE_RGBA_8 : TYPE_RGB_8;
  cmsHTRANSFORM transform =
    cmsCreateTransform(source_profile,
                       source_lcms_format(source_format),
                       target_profile,
                       output_format,
                       INTENT_PERCEPTUAL,
                       cmsFLAGS_BLACKPOINTCOMPENSATION |
                         (alpha ? cmsFLAGS_COPY_ALPHA : 0));
  cmsCloseProfile(source_profile);
  cmsCloseProfile(target_profile);
  if (!transform) {
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_FAILED,
                        "LittleCMS could not create the display transform");
    return NULL;
  }

  const guint source_width = losles_image_get_width(image);
  const guint source_height = losles_image_get_height(image);
  const guint destination_width = losles_image_get_display_width(image);
  const guint destination_height = losles_image_get_display_height(image);
  const guint output_components = alpha ? 4 : 3;
  if (destination_width > G_MAXUINT / output_components ||
      destination_height >
        G_MAXSIZE / (destination_width * output_components)) {
    cmsDeleteTransform(transform);
    g_set_error_literal(error,
                        G_IO_ERROR,
                        G_IO_ERROR_NO_SPACE,
                        "Rendered image dimensions are too large");
    return NULL;
  }

  const guint destination_stride = destination_width * output_components;
  guint8 *destination =
    g_malloc_n(destination_height, destination_stride);
  guint8 *row = g_malloc_n(source_width, output_components);
  const guint8 *source = g_bytes_get_data(losles_image_get_pixels(image),
                                          NULL);
  const guint source_stride = losles_image_get_stride(image);
  const guint orientation = losles_image_get_orientation(image);

  for (guint y = 0; y < source_height; y++) {
    if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
      cmsDeleteTransform(transform);
      g_free(row);
      g_free(destination);
      return NULL;
    }

    cmsDoTransform(transform,
                   source + (gsize)y * source_stride,
                   row,
                   source_width);
    for (guint x = 0; x < source_width; x++) {
      guint destination_x = 0;
      guint destination_y = 0;
      map_oriented_pixel(orientation,
                         source_width,
                         source_height,
                         x,
                         y,
                         &destination_x,
                         &destination_y);
      memcpy(destination +
               (gsize)destination_y * destination_stride +
               (gsize)destination_x * output_components,
             row + (gsize)x * output_components,
             output_components);
    }
  }

  cmsDeleteTransform(transform);
  g_free(row);

  g_autoptr(GBytes) destination_bytes =
    g_bytes_new_take(destination,
                     (gsize)destination_stride * destination_height);
  return losles_rendered_image_new(destination_width,
                                   destination_height,
                                   destination_stride,
                                   alpha ? LOSLES_PIXEL_FORMAT_RGBA8
                                         : LOSLES_PIXEL_FORMAT_RGB8,
                                   destination_bytes,
                                   target->name,
                                   target->id,
                                   used_embedded_profile);
}
