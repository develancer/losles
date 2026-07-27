#include "losles-application.h"

#include "losles-window.h"

struct _LoslesApplication {
  GtkApplication parent_instance;
};

G_DEFINE_FINAL_TYPE(LoslesApplication,
                    losles_application,
                    GTK_TYPE_APPLICATION)

static LoslesWindow *
get_window(LoslesApplication *self)
{
  GtkWindow *active =
    gtk_application_get_active_window(GTK_APPLICATION(self));
  if (active)
    return LOSLES_WINDOW(active);

  return losles_window_new(GTK_APPLICATION(self));
}

static void
losles_application_activate(GApplication *application)
{
  LoslesWindow *window = get_window(LOSLES_APPLICATION(application));
  gtk_window_present(GTK_WINDOW(window));
}

static void
losles_application_open(GApplication *application,
                        GFile **files,
                        gint n_files,
                        const gchar *hint)
{
  (void)hint;
  LoslesWindow *window = get_window(LOSLES_APPLICATION(application));
  gtk_window_present(GTK_WINDOW(window));
  if (n_files > 0)
    losles_window_open_file(window, files[0]);
}

static void
losles_application_class_init(LoslesApplicationClass *klass)
{
  GApplicationClass *application_class = G_APPLICATION_CLASS(klass);
  application_class->activate = losles_application_activate;
  application_class->open = losles_application_open;
}

static void
losles_application_init(LoslesApplication *self)
{
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.open",
                                        (const gchar *[]){"<Control>o", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.previous",
                                        (const gchar *[]){"Left", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.next",
                                        (const gchar *[]){"Right", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.toggle-crop",
                                        (const gchar *[]){"c", NULL});
  gtk_application_set_accels_for_action(
    GTK_APPLICATION(self),
    "win.apply-crop",
    (const gchar *[]){"Return", "KP_Enter", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.delete",
                                        (const gchar *[]){"Delete", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.toggle-info",
                                        (const gchar *[]){"i", NULL});
  gtk_application_set_accels_for_action(
    GTK_APPLICATION(self),
    "win.toggle-fullscreen",
    (const gchar *[]){"<Alt>Return", "<Alt>KP_Enter", NULL});
  gtk_application_set_accels_for_action(GTK_APPLICATION(self),
                                        "win.escape",
                                        (const gchar *[]){"Escape", NULL});
}

LoslesApplication *
losles_application_new(void)
{
  return g_object_new(LOSLES_TYPE_APPLICATION,
                      "application-id",
                      "io.github.losles.Losles",
                      "flags",
                      G_APPLICATION_HANDLES_OPEN,
                      NULL);
}
