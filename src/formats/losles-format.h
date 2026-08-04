#pragma once

#include <gio/gio.h>

#include "../losles-image.h"

G_BEGIN_DECLS

typedef enum {
  LOSLES_ROTATE_LEFT,
  LOSLES_ROTATE_RIGHT,
} LoslesRotation;

typedef enum {
  LOSLES_FORMAT_EDIT_NONE = 0,
  LOSLES_FORMAT_EDIT_ALLOW_RECOVERABLE_WARNINGS = 1 << 0,
} LoslesFormatEditFlags;

typedef enum {
  LOSLES_FORMAT_ERROR_WARNING_REQUIRES_CONFIRMATION,
} LoslesFormatError;

#define LOSLES_FORMAT_ERROR (losles_format_error_quark())
GQuark losles_format_error_quark(void);

typedef struct {
  guint x;
  guint y;
  guint width;
  guint height;
} LoslesCrop;

#define LOSLES_TYPE_FORMAT (losles_format_get_type())
G_DECLARE_INTERFACE(LoslesFormat, losles_format, LOSLES, FORMAT, GObject)

struct _LoslesFormatInterface {
  GTypeInterface parent_iface;

  const gchar *(*get_name)(LoslesFormat *self);
  gboolean (*matches)(LoslesFormat *self, GBytes *encoded);
  LoslesImage *(*load)(LoslesFormat *self,
                       GFile *file,
                       GBytes *encoded,
                       GCancellable *cancellable,
                       GError **error);
  gboolean (*supports_lossless_rotation)(LoslesFormat *self);
  gboolean (*supports_lossless_orientation_normalization)(
    LoslesFormat *self);
  gboolean (*supports_lossless_crop)(LoslesFormat *self);
  gboolean (*adjust_crop)(LoslesFormat *self,
                          LoslesImage *image,
                          const LoslesCrop *requested,
                          LoslesCrop *adjusted,
                          GError **error);
  gboolean (*rotate_lossless)(LoslesFormat *self,
                              LoslesImage *image,
                              GFile *destination,
                              LoslesRotation rotation,
                              LoslesFormatEditFlags flags,
                              GCancellable *cancellable,
                              GError **error);
  gboolean (*normalize_orientation_lossless)(LoslesFormat *self,
                                             LoslesImage *image,
                                             GFile *destination,
                                             LoslesFormatEditFlags flags,
                                             GCancellable *cancellable,
                                             GError **error);
  gboolean (*crop_lossless)(LoslesFormat *self,
                            LoslesImage *image,
                            GFile *destination,
                            const LoslesCrop *crop,
                            LoslesFormatEditFlags flags,
                            GCancellable *cancellable,
                            GError **error);
};

const gchar *losles_format_get_name(LoslesFormat *self);
gboolean losles_format_matches(LoslesFormat *self, GBytes *encoded);
LoslesImage *losles_format_load(LoslesFormat *self,
                                GFile *file,
                                GBytes *encoded,
                                GCancellable *cancellable,
                                GError **error);
gboolean losles_format_supports_lossless_rotation(LoslesFormat *self);
gboolean losles_format_supports_lossless_orientation_normalization(
  LoslesFormat *self);
gboolean losles_format_supports_lossless_crop(LoslesFormat *self);
gboolean losles_format_adjust_crop(LoslesFormat *self,
                                   LoslesImage *image,
                                   const LoslesCrop *requested,
                                   LoslesCrop *adjusted,
                                   GError **error);
gboolean losles_format_rotate_lossless(LoslesFormat *self,
                                       LoslesImage *image,
                                       GFile *destination,
                                       LoslesRotation rotation,
                                       LoslesFormatEditFlags flags,
                                       GCancellable *cancellable,
                                       GError **error);
gboolean losles_format_normalize_orientation_lossless(
  LoslesFormat *self,
  LoslesImage *image,
  GFile *destination,
  LoslesFormatEditFlags flags,
  GCancellable *cancellable,
  GError **error);
gboolean losles_format_crop_lossless(LoslesFormat *self,
                                     LoslesImage *image,
                                     GFile *destination,
                                     const LoslesCrop *crop,
                                     LoslesFormatEditFlags flags,
                                     GCancellable *cancellable,
                                     GError **error);

G_END_DECLS
