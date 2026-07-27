#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define LOSLES_TYPE_APPLICATION (losles_application_get_type())
G_DECLARE_FINAL_TYPE(LoslesApplication,
                     losles_application,
                     LOSLES,
                     APPLICATION,
                     GtkApplication)

LoslesApplication *losles_application_new(void);

G_END_DECLS
