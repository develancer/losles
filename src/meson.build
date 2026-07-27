losles_sources = files(
  'main.c',
  'losles-application.c',
  'losles-color-manager.c',
  'losles-image.c',
  'losles-rendered-image.c',
  'losles-window.c',
  'formats/losles-format.c',
  'formats/losles-format-registry.c',
  'formats/losles-jpeg-format.c',
  'formats/losles-jpeg-metadata.c',
  'formats/losles-png-format.c',
)

losles_deps = [
  gtk_dep,
  lcms_dep,
  jpeg_dep,
  png_dep,
  colord_dep,
]

losles_c_args = [
  '-DLOCALEDIR="@0@"'.format(get_option('prefix') / get_option('localedir')),
  '-DG_LOG_DOMAIN="losles"',
  '-DG_LOG_USE_STRUCTURED=1',
]

executable(
  'losles',
  losles_sources,
  dependencies: losles_deps,
  c_args: losles_c_args,
  install: true,
)
