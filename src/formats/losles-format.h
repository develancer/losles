#pragma once

#include <gio/gio.h>

#include "../losles-image.h"

G_BEGIN_DECLS

typedef enum {
  LOSLES_ROTATE_LEFT,
  LOSLES_ROTATE_RIGHT,
} LoslesRotation;

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
                              GCancellable *cancellable,
                              GError **error);
  gboolean (*crop_lossless)(LoslesFormat *self,
                            LoslesImage *image,
                            GFile *destination,
                            const LoslesCrop *crop,
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
                                       GCancellable *cancellable,
                                       GError **error);
gboolean losles_format_crop_lossless(LoslesFormat *self,
                                     LoslesImage *image,
                                     GFile *destination,
                                     const LoslesCrop *crop,
                                     GCancellable *cancellable,
                                     GError **error);

G_END_DECLS
