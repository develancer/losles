<p align="center">
  <img src="data/icons/hicolor/512x512/apps/io.github.develancer.losles.png"
       alt="losles icon"
       width="160">
</p>

# losles

**losles** is a small, color-managed photo viewer for JPEG and PNG images. It
uses embedded ICC profiles, converts images directly to the profile selected
for the current monitor, and provides carefully constrained lossless rotation
and cropping.

The
[latest GitHub release](https://github.com/develancer/losles/releases/latest)
provides prebuilt x86-64 packages for Ubuntu 24.04, Ubuntu 26.04, Debian 13,
and Windows 10/11.

losles is written in C17 with GTK 4. LittleCMS handles color conversion,
libjpeg-turbo provides coefficient-level JPEG transforms, and libpng handles
PNG decoding and editing.

## Why losles?

Because every other photo viewer was missing something I considered a
must-have, while providing a lot of clutter I did not need. This one focuses
on ICC profile support, lossless operations, speed through preloading, and not
much more.

## Install a release

Download packages from the
[latest release page](https://github.com/develancer/losles/releases/latest).
The release also contains `SHA256SUMS` for all four installable artifacts.

### Ubuntu and Debian

Use the package built for your exact distribution:

| System | Package |
| --- | --- |
| Ubuntu 24.04, x86-64 | `losles_<version>-0losles1~ubuntu24.04.1_amd64.deb` |
| Ubuntu 26.04, x86-64 | `losles_<version>-0losles1~ubuntu26.04.1_amd64.deb` |
| Debian 13, x86-64 | `losles_<version>-0losles1~debian13.1_amd64.deb` |

After downloading the matching package and `SHA256SUMS` into the same
directory, optionally verify it:

```sh
sha256sum --check --ignore-missing SHA256SUMS
```

Install it with APT so dependencies are obtained from the distribution. For
example, from a directory containing only the downloaded Ubuntu 24.04
package:

```sh
sudo apt install ./losles_*~ubuntu24.04.1_amd64.deb
```

Use the corresponding filename suffix on Ubuntu 26.04 or Debian 13. The
packages are standalone GitHub downloads, not an APT repository; they do not
configure an additional package source. Remove the application with:

```sh
sudo apt remove losles
```

These are upstream convenience packages, not yet packages from the official
Debian or Ubuntu archives. Their version sorts below a future official Debian
revision, so an archive package can replace them normally.

### Windows 10 and 11

Download and run the file named
`losles-<version>-windows-x86_64-setup.exe`.

The installer is per-user, does not request administrator privileges, and
installs under `%LOCALAPPDATA%\Programs\losles`. It creates a Start Menu
shortcut, appears in Apps & Features, and makes losles available in JPEG and
PNG “Open with” lists without changing the default viewer.

The installer is not Authenticode-signed, so Windows SmartScreen may warn
before running it. Its SHA-256 value can be compared with the release's
`SHA256SUMS`. Uninstall losles from Windows Settings → Apps → Installed apps.

## Features

- Embedded JPEG `ICC_PROFILE` data, including profiles split over multiple
  APP2 markers.
- Embedded PNG `iCCP` profiles.
- Direct conversion to the RGB ICC profile selected for the current monitor
  on Ubuntu/Debian or Windows, including wide-gamut profiles.
- Explicit, visible sRGB fallback when no usable monitor profile is available.
- Correct display of all eight JPEG EXIF orientation values.
- Coefficient-lossless JPEG rotation, orientation normalization, and
  MCU-aligned crop through libjpeg-turbo.
- Pixel-lossless rotation and exact-pixel crop for common static,
  non-interlaced, direct-color 8-bit PNG files.
- Preservation of JPEG marker metadata and non-image-data PNG chunks, subject
  to the metadata-consistency caveat below.
- Background decode and color conversion of nearby images for fast browsing.
- Cursor-anchored wheel zoom, mouse panning, fullscreen, drag-and-drop
  opening, and mouse Back/Forward navigation.

Images without an embedded profile use an explicit source assumption: RGB is
treated as sRGB, while grayscale is treated as D65 gamma 2.2. Invalid or
incompatible embedded profiles use the same fallback and are reported as an
assumption in the information overlay.

## Using losles

Open a file from the application, pass it on the command line, choose losles
from a file manager's “Open with” menu, or drag a file onto the window:

```sh
losles /path/to/photo.jpg
```

When one image is opened, losles scans its directory for `.jpg`, `.jpeg`,
`.jpe`, and `.png` files and sorts them by filename using the current locale.

| Action | Control |
| --- | --- |
| Open an image | `Ctrl+O` or drag a file onto the window |
| Previous / next image | `Left` / `Right`, header buttons, or mouse Back / Forward |
| Zoom at the pointer | Mouse wheel |
| Pan a zoomed image | Primary-button drag |
| Reset zoom | `Escape` when Crop mode is inactive |
| Toggle information overlay | `I` or the information button |
| Enter or leave fullscreen | Double-click the picture or `Alt+Enter` |
| Enter or leave Crop mode | `C` or the crop button |
| Apply the crop selection | `Enter` or the visible Crop button |
| Move the current file to Trash | `Delete` |

`Escape` deliberately does not leave fullscreen. It first leaves Crop mode;
otherwise it resets a zoomed image to its fitted size.

The information overlay is hidden by default and appears at the bottom-left
of the image. It shows the format, displayed dimensions, source-profile
choice, and selected display target.

While browsing, losles retains the current image and up to five images on each
side in both its decoded-image and display-converted caches. Each cache has a
soft limit of 10% of total system memory, and no more than two background
decodes and two background color conversions run at once. When adjacent
images have identical displayed dimensions, zoom and pan are preserved.

The previous image remains visible under a spinner while an uncached
replacement is prepared. A fully decoded and color-converted cache hit appears
immediately without a spinner.

## Lossless editing and file safety

Editing is immediate and in place. Read these rules before relying on the
editing controls:

- **Rotate left/right** prepares the transformed file and then overwrites the
  original path. A normal, warning-free rotation does not keep a backup in
  Trash.
- **Normalize EXIF orientation** also overwrites the original without a Trash
  backup when TurboJPEG reports no warning.
- **Crop** first prepares the result, then moves the exact original to the
  system Trash before installing the cropped file at the original path.
- **Delete** moves the current file to the system Trash and then opens its
  successor, or its predecessor when the deleted file was last.

Editing requires a regular local file. Rotation and normalization complete
their transformed output before replacing the source. Crop refuses to proceed
if the original cannot be moved to Trash. If TurboJPEG reports a warning for
any JPEG edit, losles leaves the source untouched and asks whether to continue.
An approved retry also requires Trash and preserves the exact original there,
including for rotation and EXIF-orientation normalization.

### JPEG

JPEG transforms rearrange DCT coefficients through libjpeg-turbo; they do not
decode and re-encode lossy image data. A mathematically perfect transform is
required. If incomplete edge blocks would need to be discarded, the operation
fails rather than silently trimming pixels.

For every JPEG operation, losles first asks TurboJPEG to stop on warnings. If
that strict attempt reports one, the source remains unchanged and a dialog
shows the warning. Choosing Continue retries the same perfect coefficient
transform while allowing warnings; losles does not attempt to prove that the
result is complete or undamaged. The exact original is therefore moved to
Trash before the replacement is installed. Choosing Cancel leaves it in
place.

Ordinary rotation respects and retains the existing EXIF orientation value.
The warning-icon Normalize control is enabled only when a valid non-default
orientation tag is present. It applies the stored orientation to the JPEG
coefficients and sets that existing tag to `1` without changing the displayed
image. Cropping is available only at orientation `1`; use Normalize first when
needed.

JPEG crop selections snap to legal MCU boundaries while they are dragged,
moved, or resized. The rectangle shown on screen is the rectangle that will
be written.

JPEG APP and COM markers—including ICC, EXIF, XMP, IPTC, comments, and
unrecognized application markers—are retained. For structurally valid files
and supported perfect transforms, rotating right and then left reproduces the
encoded file byte-for-byte. A warning-approved edit of a damaged file may
normalize damaged entropy-stream bytes and may produce incomplete or damaged
output; use the original retained in Trash if the result is not acceptable.

### PNG

PNG edits decode and recompress the original samples without changing their
values. `IHDR` and `IDAT` are regenerated as required; every other original
chunk is copied byte-for-byte and in order. The resulting PNG is
pixel-lossless but is not expected to have identical compressed `IDAT` bytes.

Editing is enabled only for static, non-interlaced, 8-bit grayscale,
grayscale-alpha, RGB, and RGBA PNG files. Palette PNGs, sub-8-bit grayscale,
16-bit PNGs, Adam7-interlaced PNGs, and animated PNGs remain view-only.

### Metadata caveat

Metadata whose meaning depends on image dimensions is generally preserved
rather than regenerated. The libjpeg-turbo transform may update recognized
EXIF pixel-dimension fields during a JPEG transform, but embedded thumbnails
and other dimension-dependent JPEG metadata can still describe the pre-edit
image. Dimension- or position-dependent PNG ancillary fields are copied
unchanged and can do the same. The actual encoded image dimensions are
updated correctly.

## Color management

losles performs application-managed SDR display conversion with LittleCMS.
Source pixels are converted directly from the embedded or assumed source
profile into the active monitor's RGB profile. Wide-gamut output is not
collapsed through sRGB first.

On Ubuntu and Debian, losles matches the GTK monitor connector to a colord
display device and uses that device's selected/default profile. Moving the
window to another monitor or changing the selected colord profile invalidates
the display-converted cache and renders the current image again.

On Windows, losles obtains the output profile for the monitor containing the
window through the Win32 color-management API. It rechecks the profile when a
render starts or the window moves between monitors. Changing the Windows
profile while the same image remains idle does not redraw immediately;
navigating or moving between monitors triggers a recheck.

Both paths use 8-bit SDR output. Wide-gamut ICC support does not imply HDR or
high-bit-depth display. If no valid RGB monitor profile can be loaded, losles
uses a built-in sRGB target and reports the fallback in the information
overlay.

For Linux display-profile diagnostics:

```sh
G_MESSAGES_DEBUG=losles losles /path/to/photo.jpg
```

## Current limitations

- Viewing supports grayscale and RGB JPEG; CMYK and YCCK JPEG are rejected.
- PNG palette, low-bit-depth, 16-bit, transparency, and interlaced inputs can
  be viewed, but 16-bit samples are reduced to 8-bit for display.
- Animated images are not played.
- Display output is 8-bit SDR; there is no HDR pipeline.
- Zoom cannot go below the initial fitted size.
- There is no file-change monitor. External edits do not invalidate cached
  images during the current browsing session.
- There is no recursive directory scan, thumbnail view, recent-files list,
  settings UI, or runtime plugin loader.
- The interface is currently English only.
- Release packages are currently available only for x86-64.

## Build from source

Installing the matching release package is recommended for normal use. A
source build is useful for development or unsupported distributions.

### Ubuntu 24.04 or later and Debian 13

Install the build dependencies from the standard distribution repositories:

```sh
sudo apt install \
  build-essential pkg-config \
  libgtk-4-dev liblcms2-dev libcolord-dev \
  libjpeg-dev libturbojpeg0-dev libpng-dev
```

Then build, test, and run:

```sh
make -j2
make test
make run ARGS=/path/to/photo.jpg
```

The Makefile detects dependencies with `pkg-config` and writes generated
output under `build`. A Git checkout derives its version from the nearest
release tag. GitHub-generated source archives do not contain Git metadata, so
pass the release version to every Make invocation when building one:

```sh
release_version=YYYY.MM.N  # replace with the archive's release version
make -j2 VERSION="$release_version"
make VERSION="$release_version" test
```

To install a source build under `/usr/local`:

```sh
build_version="$(make --no-print-directory print-version)"
sudo make VERSION="$build_version" install
```

Resolving the version before `sudo` avoids Git repository-ownership checks
under the root account. When installing from a GitHub-generated source
archive, reuse the value set above:

```sh
sudo make VERSION="$release_version" install
```

This direct installation is not tracked by APT and the Makefile currently has
no uninstall target. Prefer the `.deb` package when one matches the system.
For a non-privileged installation smoke test:

```sh
make DESTDIR=/tmp/losles-stage install
```

Use a separate build directory for sanitizer builds because Make does not
record compiler-option changes:

```sh
make BUILD_DIR=build-asan \
  SANITIZE=address,undefined \
  test
```

Run `make help` for other targets and overrides.

### Windows development build

Open an MSYS2 **UCRT64** shell and install:

```sh
pacman -S --needed \
  git make \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-gtk4 \
  mingw-w64-ucrt-x86_64-lcms2 \
  mingw-w64-ucrt-x86_64-libjpeg-turbo \
  mingw-w64-ucrt-x86_64-libpng
```

Build and test from that shell:

```sh
make -j2
make test
./build/losles.exe /path/to/photo.jpg
```

The Makefile recognizes the MinGW compiler, selects the Win32 platform layer,
omits colord, adds `.exe`, and embeds the native multi-resolution icon.
Creating the distributable NSIS installer is handled by the tagged GitHub
Actions workflow rather than the normal developer build.

## Versions and releases

Release versions and Git tags use `YYYY.MM.N`, where `N` counts releases
within the month. A clean checkout at a release tag builds with that exact
version; later commits append the distance and abbreviated commit ID, as in
`YYYY.MM.N+3.g1a2b3c4d5e6f`. Tracked local changes add a `.dirty` suffix.

Pushing a valid release tag runs the GitHub Actions release workflow. It
builds and tests in Ubuntu 24.04, Ubuntu 26.04, Debian 13, and Windows 2025
environments, then publishes the three `.deb` files, the Windows installer,
and `SHA256SUMS` in one GitHub Release. The checked-in Debian changelog remains
at a neutral packaging-work version; the workflow creates target-qualified
package versions only in its private build checkouts.

## License and contributing

losles and its application icon are distributed under the MIT License. The
AppStream metadata is separately dedicated under CC0-1.0. See
[`COPYING`](COPYING).

Issues and patches are welcome at
[`github.com/develancer/losles`](https://github.com/develancer/losles).
Technical contributors and AI agents should read
[`AGENTS.md`](AGENTS.md) before changing the project.
