#pragma once

#include <gdk/gdk.h>

#include "losles-image.h"
#include "losles-rendered-image.h"

G_BEGIN_DECLS

typedef struct _LoslesColorTarget LoslesColorTarget;

LoslesColorTarget *losles_color_target_new_for_profile(
  GBytes *profile,
  const gchar *name,
  const gchar *id,
  GError **error);
LoslesColorTarget *losles_color_target_ref(LoslesColorTarget *self);
void losles_color_target_unref(LoslesColorTarget *self);
const gchar *losles_color_target_get_name(LoslesColorTarget *self);
const gchar *losles_color_target_get_id(LoslesColorTarget *self);
gboolean losles_color_target_is_fallback(LoslesColorTarget *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(LoslesColorTarget, losles_color_target_unref)

#define LOSLES_TYPE_COLOR_MANAGER (losles_color_manager_get_type())
G_DECLARE_FINAL_TYPE(LoslesColorManager,
                     losles_color_manager,
                     LOSLES,
                     COLOR_MANAGER,
                     GObject)

LoslesColorManager *losles_color_manager_new(void);
LoslesColorTarget *losles_color_manager_get_target(LoslesColorManager *self,
                                                   GdkMonitor *monitor,
                                                   GdkSurface *surface);
LoslesRenderedImage *losles_color_target_render(LoslesColorTarget *target,
                                                LoslesImage *image,
                                                GCancellable *cancellable,
                                                GError **error);

G_END_DECLS
