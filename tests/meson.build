if get_option('tests')
  jpeg_metadata_test = executable(
    'test-jpeg-metadata',
    [
      'test-jpeg-metadata.c',
      '../src/formats/losles-jpeg-metadata.c',
    ],
    include_directories: include_directories('../src'),
    dependencies: gtk_dep,
  )

  test('jpeg-metadata', jpeg_metadata_test)

  format_test = executable(
    'test-formats',
    [
      'test-formats.c',
      '../src/losles-color-manager.c',
      '../src/losles-image.c',
      '../src/losles-rendered-image.c',
      '../src/formats/losles-format.c',
      '../src/formats/losles-format-registry.c',
      '../src/formats/losles-jpeg-format.c',
      '../src/formats/losles-jpeg-metadata.c',
      '../src/formats/losles-png-format.c',
    ],
    include_directories: include_directories('../src'),
    dependencies: [
      gtk_dep,
      lcms_dep,
      jpeg_dep,
      png_dep,
      colord_dep,
    ],
  )

  test('formats', format_test, timeout: 30)
endif
