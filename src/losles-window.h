#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define LOSLES_TYPE_WINDOW (losles_window_get_type())
G_DECLARE_FINAL_TYPE(LoslesWindow,
                     losles_window,
                     LOSLES,
                     WINDOW,
                     GtkApplicationWindow)

LoslesWindow *losles_window_new(GtkApplication *application);
void losles_window_open_file(LoslesWindow *self, GFile *file);

G_END_DECLS
