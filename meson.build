project(
  'losles',
  'c',
  version: '0.1.0',
  meson_version: '>= 1.3.0',
  default_options: [
    'c_std=c17',
    'warning_level=3',
    'werror=false',
    'buildtype=debugoptimized',
  ],
)

gnome = import('gnome')

# GTK 4.14's public gdkdmabufformats.h contains one extra top-level
# semicolon that triggers GCC's pedantic warning in every GTK translation
# unit. Other high-level warnings remain enabled.
add_project_arguments('-Wno-pedantic', language: 'c')

gtk_dep = dependency('gtk4', version: '>= 4.10')
lcms_dep = dependency('lcms2', version: '>= 2.14')
jpeg_dep = dependency('libjpeg')
png_dep = dependency('libpng', version: '>= 1.6')
colord_dep = dependency('colord', version: '>= 1.4.6')

subdir('src')
subdir('data')
subdir('tests')

summary(
  {
    'Version': meson.project_version(),
    'GTK': gtk_dep.version(),
    'Color management': 'embedded ICC -> active colord display ICC (LittleCMS)',
    'JPEG lossless helper': 'jpegtran (runtime)',
  },
  section: 'losles',
)
