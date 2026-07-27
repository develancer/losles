#include <locale.h>

#include "losles-application.h"

int
main(int argc, char **argv)
{
  setlocale(LC_ALL, "");
  g_autoptr(LoslesApplication) application = losles_application_new();
  return g_application_run(G_APPLICATION(application), argc, argv);
}
