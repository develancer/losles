# losles

Losles is a small, color-managed photo viewer for Ubuntu 24.04 and later. It
currently displays JPEG and PNG files and performs coefficient-level,
lossless JPEG rotation and cropping.

The application is deliberately written in C17. On Ubuntu this avoids a
language runtime, an async executor, and format frameworks that would overlap
with functionality losles needs to control itself. GTK 4 supplies the native
desktop integration, LittleCMS performs color conversion, libjpeg-turbo and
libpng decode pixels, and colord supplies the selected monitor profile.

## What works

- Embedded JPEG `ICC_PROFILE` data, including profiles split over multiple
  APP2 markers.
- Embedded PNG `iCCP` profiles.
- Application-side conversion from the embedded image profile to the ICC
  profile selected for the monitor in Ubuntu Settings.
- Wide-gamut RGB display profiles. Output values are encoded directly in the
  monitor profile's RGB space; the image is not collapsed to sRGB first.
- Correct display of all eight EXIF orientation values.
- Lossless JPEG rotation and MCU-aligned cropping through libturbojpeg, while
  preserving JPEG marker metadata including ICC, EXIF, XMP, IPTC, comments,
  and unknown APP markers.
- Explicit lossless EXIF-orientation normalization: the displayed orientation
  is baked into JPEG coefficients and the existing orientation tag is set to
  `1`.
- Fast previous/next navigation. Losles decodes and color-converts up to two
  images on either side in the background. Decoded sources and
  display-profile textures have separate 512 MiB cache limits, with at most
  two background decodes and two background color conversions at once.
- A format-module interface. JPEG and PNG are separate GObject
  implementations, so another decoder/editor does not need changes to the
  window or color pipeline.

Images without an embedded profile are treated as sRGB. Grayscale PNG files
without a profile use a D65 gamma-2.2 gray profile. If no selected monitor ICC
profile can be found, losles uses an explicit sRGB display fallback and says
so in the information overlay.

## Ubuntu 24.04 build

All dependencies are in the standard Ubuntu repositories:

```sh
sudo apt install \
  build-essential pkg-config \
  libgtk-4-dev liblcms2-dev libcolord-dev \
  libjpeg-dev libturbojpeg0-dev libpng-dev

make
make test
./build/losles /path/to/photo.jpg
```

The Makefile uses `pkg-config` to find the system libraries and writes all
output under `build` by default. Installation follows the usual Make
variables and honors `DESTDIR`:

```sh
sudo make install                 # prefix=/usr/local by default
make DESTDIR=/tmp/losles-stage install
```

For AddressSanitizer and UndefinedBehaviorSanitizer, use a separate output
directory because Make does not track changes to compiler flags:

```sh
make BUILD_DIR=build-asan \
  SANITIZE=address,undefined \
  test
```

Run `make help` for the available targets and overrides.

## Lossless editing semantics

Rotation and crop are applied in place as soon as their action button is
clicked. Rotation first creates the transformed JPEG and then overwrites the
source path; it does not create a backup in the system Trash.

Crop first creates the transformed JPEG and a safety backup, then moves the
exact original file into the system Trash and installs the cropped file at the
original path. If the original cannot be moved to Trash, it is left unchanged
and the crop fails.

JPEG transforms operate on DCT coefficients through libturbojpeg, not by
decoding and re-encoding pixels. JPEG marker metadata is copied without
selectively rewriting or discarding fields. Ordinary left/right rotation
retains the original EXIF orientation tag. For mirrored EXIF orientations,
Losles reverses the raw coefficient direction so that the result still rotates
in the direction requested on screen. A right rotation followed by a left
rotation is byte-identical for the supported perfect-transform path.

The separate warning-icon normalization button is enabled only for a JPEG
containing an EXIF orientation tag whose value is not `1`. It applies that
orientation to the coefficients and changes the existing tag to `1`, without
changing the displayed image. Its tooltip explains whether a non-default
orientation is present. Like ordinary rotation, normalization overwrites the
source without creating a Trash backup. This explicit action is useful for
software that ignores EXIF orientation and enables Losles cropping afterward.

JPEG crop coordinates must start on an MCU boundary. While the rectangle is
drawn, moved, or resized, Losles snaps it to the format module's nearest legal
boundaries. The visible rectangle is therefore the rectangle that will be
applied; clicking Crop does not perform a second hidden expansion. A transform
is refused when a mathematically perfect result is impossible; losles does not
silently trim edge pixels.

Metadata fields whose meaning depends on image dimensions, such as an embedded
thumbnail or EXIF pixel dimensions, are retained unchanged rather than
discarded or regenerated. They can therefore describe the pre-edit image after
a crop, a quarter-turn rotation, or orientation normalization. The JPEG's own
encoded dimensions are updated correctly.

PNG editing is intentionally disabled in this first version. Although PNG
recompression is pixel-lossless, preserving 16-bit samples and all relevant
ancillary metadata needs a dedicated writer. Calling an 8-bit export
"lossless" would be misleading.

## Interface

Image and color-profile information is shown on an opaque black overlay
anchored directly to the bottom-left corner of the picture, so it does not
consume layout space. It is hidden by default and can be toggled with the
information button or the `I` key. The image canvas is always pure black,
independently of the desktop color scheme and in fullscreen.

Double-click the picture or press `Alt+Enter` to enter or leave fullscreen.
The header bar is hidden while fullscreen is active. `Escape` does not leave
fullscreen; it only cancels crop mode when crop mode is active.

Press `C` to enter or leave crop mode, including while fullscreen. In crop
mode, drag over the image to create a selection. Drag inside an existing
selection to move it, or drag any of its four edges or eight visible
edge/corner handles to resize it. The handles move in lossless JPEG block
increments, so the overlay always previews the exact crop result. Press
`Enter` to apply a valid selection.

Use the warning-icon orientation control when it is enabled to bake a
non-default EXIF orientation into the JPEG coefficients and set the tag to
`1`. The image keeps the same visual orientation, and the operation is
coefficient-lossless.

Press `Delete` to move the current image to the system Trash. Losles then
opens the next image in directory order when one exists. If there is no next
image, it clears the browsing session and displays “No picture opened.”

An image file can also be opened by dragging its file icon from Nautilus onto
the Losles window. A multi-file drop opens the first file. File drops are
rejected while Crop mode or another file operation is active.

## Color-management model on Ubuntu 24.04

Losles does not depend on Mutter's newer color-management protocol. GTK 4.14
on Ubuntu 24.04 cannot attach an ICC colorspace to a surface, so losles uses
the established application-managed path:

1. Obtain the active `GdkMonitor` and its connector name (for example
   `DP-0`).
2. Find the corresponding colord display device by its `XRANDR_name`
   metadata.
3. Read that device's selected/default RGB profile.
4. Use LittleCMS to convert the decoded source pixels from their embedded
   profile directly into the monitor profile.
5. Give those already converted device-RGB values to GTK 4.14.

Moving the window to another monitor or changing the device's selected
profile invalidates the current transform and rebuilds the visible texture.
Calibration curves (VCGT) remain the desktop color service's responsibility
and are not applied a second time by losles.

This design targets the Ubuntu 24.04 SDR desktop pipeline. It supports the
gamut described by an RGB monitor ICC profile, but it is not an HDR
implementation. On a future compositor that color-transforms every surface,
losles will need a second backend that tags source pixels instead of
preconverting them; otherwise conversion could be applied twice.

## Source layout

```text
src/
  losles-window.c             UI, navigation, two-level cache, background jobs
  losles-color-manager.c      colord lookup and LittleCMS transforms
  losles-image.c              format-neutral decoded image
  formats/
    losles-format.c           module interface
    losles-format-registry.c  format selection
    losles-jpeg-format.c      JPEG decode and lossless writer
    losles-png-format.c       PNG decode
tests/
  test-jpeg-metadata.c
  test-formats.c              ICC/render, TurboJPEG, and Trash integration
```

Current deliberate limits are RGB/grayscale JPEG and PNG viewing, JPEG-only
editing, 8-bit display buffers, SDR ICC profiles, and local files for
editing. CMYK JPEG display is not implemented yet.
