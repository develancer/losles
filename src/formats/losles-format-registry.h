#pragma once

#include "losles-format.h"

G_BEGIN_DECLS

#define LOSLES_TYPE_FORMAT_REGISTRY (losles_format_registry_get_type())
G_DECLARE_FINAL_TYPE(LoslesFormatRegistry,
                     losles_format_registry,
                     LOSLES,
                     FORMAT_REGISTRY,
                     GObject)

LoslesFormatRegistry *losles_format_registry_new(void);
LoslesImage *losles_format_registry_load(LoslesFormatRegistry *self,
                                         GFile *file,
                                         GCancellable *cancellable,
                                         GError **error);
gboolean losles_format_registry_supports_file(GFile *file);

G_END_DECLS
