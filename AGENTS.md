# AGENTS.md

This file is the technical source of truth for AI agents working on losles. It
applies to the entire repository. Read it completely before changing the
project, then use `README.md` for the supported user-facing behavior and
installation story.

## Maintenance contract

Keep this file accurate. Update or extend it in the same change whenever work
changes any of the following:

- supported platforms, image formats, dependencies, or build/test commands;
- directory layout, ownership between components, or public interfaces;
- decoded pixel formats, ICC behavior, display-profile selection, or rendering;
- lossless-editing semantics, metadata handling, or file-safety guarantees;
- navigation, asynchronous jobs, cancellation, cache sizes, or invalidation;
- application ID, installed metadata, shortcuts, user-visible limits, or known
  quirks.

Update `README.md` as well when a change affects users. Keep this document
descriptive of checked-in behavior: distinguish implemented behavior, known
limitations, and future ideas. Do not leave speculative architecture recorded
as though it already exists. If a subdirectory later needs specialized agent
instructions, add a nested `AGENTS.md`; its instructions should complement
this root file rather than silently contradicting it.

## Product intent and non-negotiable properties

losles is a small, responsive, color-managed photo viewer for Ubuntu 24.04
and later, Debian 13, and x86-64 Windows 10/11. Releases use `YYYY.MM.N`
calendar versions, the code is MIT-licensed, and the implementation is
deliberately C17 with GTK 4 rather than a runtime-heavy application stack.

Preserve these product properties unless the project owner explicitly changes
them:

- Images with embedded ICC profiles must be interpreted using those profiles.
- Pixels must be converted directly into the active monitor's selected RGB ICC
  profile. Wide-gamut display profiles must not be collapsed through sRGB.
- Images without a usable embedded profile need an explicit, documented
  fallback.
- Ubuntu 24.04 must work. Do not require a newer Mutter color-management
  protocol or a newer GTK surface-color API.
- An operation advertised as lossless must not decode and re-encode lossy
  image data. Never silently trim image edges to make a transform possible.
- Rotation and crop are intentionally in place. Rotation prepares the
  transformed file and directly overwrites the source without creating a
  Trash backup. Crop prepares the transformed file, moves the exact original
  into the system Trash, then installs the cropped file at its original path.
- JPEG marker metadata must be retained. Do not discard EXIF, XMP, IPTC, ICC,
  COM, or unrecognized APP markers. Ordinary rotation retains EXIF
  orientation. Only the explicit Normalize action may bake it into the
  coefficients and set the existing orientation tag to `1`.
- Editable PNGs must retain every original chunk other than the necessarily
  regenerated `IHDR` and `IDAT` chunks byte-for-byte and in order. Never
  reduce sample precision or silently convert a view-only PNG into the
  editable subset.
- Image formats stay behind the `LoslesFormat` module interface so adding a
  decoder/editor does not require format-specific logic in the window or color
  manager.
- Navigation should favor low latency. Adjacent images are decoded and
  color-converted in advance, within bounded memory and concurrency limits.
- Prefer libraries shipped by the default Ubuntu repositories and the MSYS2
  UCRT64 repository. Avoid vendored frameworks or a new language runtime
  without a concrete, measured reason.

## Supported behavior at a glance

- Viewing: grayscale and RGB JPEG; grayscale, grayscale-alpha, RGB, RGBA,
  palette, low-bit-depth, 16-bit, transparent, and interlaced PNG. PNG inputs
  are normalized to 8-bit display buffers, and animations are not played.
- JPEG ICC: `ICC_PROFILE` APP2 chunks, including multi-marker profiles.
- PNG ICC: `iCCP`.
- Orientation: all eight JPEG EXIF orientation values are displayed.
- Editing: coefficient-level JPEG rotation, EXIF-orientation normalization,
  and MCU-aligned JPEG crop through libturbojpeg; pixel-lossless rotation and
  exact-pixel crop for static, non-interlaced, direct-color 8-bit PNGs.
- Display color: selected/default colord profile on Linux or Win32 output
  profile on Windows for the window's current monitor, with a built-in sRGB
  output fallback.
- Navigation: the opened file plus supported regular files in its parent
  directory, sorted using `g_utf8_collate()`.
- Viewing interaction: cursor-anchored wheel zoom from fit to 16× and
  primary-button panning on overflowing axes. Adjacent navigation preserves
  zoom and normalized pan when the old and new displayed dimensions match.

Important current exclusions include CMYK/YCCK JPEG display, editing of
palette/sub-8-bit/16-bit/interlaced/animated PNGs, 16-bit display buffers,
HDR, animations, free rotation, and zooming out below the initial fit.

Published releases contain x86-64 packages for Ubuntu 24.04, Ubuntu 26.04,
and Debian 13 plus one x86-64 Windows NSIS installer and `SHA256SUMS`. They
are upstream convenience packages, not packages in the Debian or Ubuntu
archives. `README.md` is the canonical installation guide; keep it based on
stable filename patterns and the GitHub `releases/latest` URL so routine
releases do not require documentation-only version updates.

## Build, test, and local packaging

### Toolchain and dependencies

Build system: GNU Make. The Makefile defaults to `-O2 -g`, compiles as C17,
and keeps generated output outside the source tree.

Supported platform baselines are Ubuntu 24.04 or later, Debian 13, and x86-64
Windows 10/11 built in MSYS2 UCRT64. Windows compilation defines
`_WIN32_WINNT` and `WINVER` as `0x0a00`, matching that baseline and exposing
`IFileOperation`'s recycle-on-delete flags.

Compile/link dependencies:

- GTK 4 (`gtk4 >= 4.10`; Ubuntu 24.04 currently supplies 4.14);
- GLib/GIO/GObject and GDK through GTK;
- LittleCMS 2 (`lcms2 >= 2.14`);
- colord (`colord >= 1.4.6`, Linux only);
- libjpeg, normally libjpeg-turbo on Ubuntu;
- TurboJPEG, the public lossless-transform API from libjpeg-turbo;
- libpng (`libpng >= 1.6`).

Windows compilation also uses GNU `windres`, supplied by the UCRT64 GCC
toolchain, to embed the native application icon. The tagged Windows packaging
job uses the UCRT64 `mingw-w64-ucrt-x86_64-nsis` package; NSIS is not required
for an ordinary developer build or for Linux.

Runtime integration:

- a running colord service and a display device/profile association are needed
  for monitor-specific Linux output;
- Windows uses GTK's native Win32 surface handle plus `GetICMProfileW`; shell
  APIs provide the Recycle Bin transaction;
- viewing still works with the built-in sRGB target when platform lookup
  fails;
- a graphical GTK session is needed to exercise the application itself.

Install the Ubuntu dependencies with:

```sh
sudo apt install \
  build-essential pkg-config \
  libgtk-4-dev liblcms2-dev libcolord-dev \
  libjpeg-dev libturbojpeg0-dev libpng-dev
```

### Normal developer build

Normal build, test, and run:

```sh
make
make test
./build/losles /path/to/photo.jpg
```

The Makefile uses `pkg-config`, generates header dependency files, and keeps
normal output in `build`. It supports `prefix`, `bindir`, `datadir`,
`applicationsdir`, `metainfodir`, `icondir`, `localedir`, `DESTDIR`,
`mandir`, `man1dir`, `BUILD_DIR`, `VERSION`, `CFLAGS`, `LDFLAGS`, `LDLIBS`,
`SOURCE_ICON_FILE`, `WINDRES`, and `SANITIZE` overrides. The checked-in
`.gitignore` covers `build` and `build-*`; a differently named `BUILD_DIR`
may need a local ignore rule.

Use a distinct `BUILD_DIR` after changing compiler/sanitizer modes because
Make does not record command-line flag changes:

```sh
make BUILD_DIR=build-asan \
  SANITIZE=address,undefined \
  test
```

LeakSanitizer aborts under some ptraced/containerized agent environments even
after every test assertion passes. In that specific environment, verify
AddressSanitizer/UBSan with:

```sh
ASAN_OPTIONS=detect_leaks=0 \
  make BUILD_DIR=build-asan \
    SANITIZE=address,undefined \
    test
```

This workaround disables only leak detection and is not a substitute for a
normal LeakSanitizer run on an unrestricted host.

The normal build directories and their generated files must not be committed.
Installation uses the normal Make prefix, `/usr/local` by default:

```sh
build_version="$(make --no-print-directory print-version)"
sudo make VERSION="$build_version" install
```

A source-tree installation is not tracked by APT and there is currently no
`uninstall` target. Recommend a matching `.deb` to ordinary users. For a
privileged source install, resolve and pass `VERSION` before `sudo` as shown:
this avoids running Git version discovery as root and its possible
`safe.directory` rejection. For a non-privileged installation smoke test, use
a temporary `DESTDIR`.

### Version derivation

The build version is generated into
`build/generated/losles-version.h` by `tools/version.sh`. Run
`make print-version` to inspect it. Release tags must be exactly
`YYYY.MM.N`: the year has four digits, the month is zero-padded from `01` to
`12`, and the within-month release counter starts at `1`. A clean commit at
such a tag gets that plain version. A later commit uses
`YYYY.MM.N+<distance>.g<hash>`, with `.dirty` appended for tracked worktree
changes. With no reachable release tag, the result is
`0+untagged.g<hash>`; outside a Git checkout it is `0+unknown`. Distribution
builds without `.git` must pass `VERSION=YYYY.MM.N` explicitly on every Make
invocation that can regenerate the version header.
If a `.git` directory or worktree link is present but `git rev-parse` fails,
`tools/version.sh` deliberately prints the underlying Git diagnostic and
exits unsuccessfully rather than misclassifying the checkout as a source
archive.

The checked-in Debian changelog deliberately stays at the neutral
`0~unreleased-1` packaging-work version until an actual Debian upload is
prepared. The tag workflow uses `dch` only in its private runner checkout to
replace that top entry with a target-qualified version:
`YYYY.MM.N-0losles1~ubuntu24.04.1`,
`YYYY.MM.N-0losles1~ubuntu26.04.1`, or
`YYYY.MM.N-0losles1~debian13.1`. Do not require upstream release tags to
update the committed Debian changelog. These `-0losles1~...` revisions sort
before Debian's eventual `-1`, so an archive package upgrades the convenience
GitHub builds.

### Debian package checks

For a local Ubuntu binary-package check after installing the
`debian/control` build dependencies:

```sh
dpkg-checkbuilddeps
DEB_BUILD_OPTIONS=parallel=2 dpkg-buildpackage -b -us -uc
lintian --display-info --pedantic ../losles_0~unreleased-1_amd64.changes
```

With the committed placeholder changelog, that command creates a local
`0~unreleased-1` package. The binary package, build information, and changes
file are written one directory above the source tree by
`dpkg-buildpackage`. Debian-generated files inside `debian/` are ignored by
`debian/.gitignore`; do not commit them. Until an ITP bug exists, Lintian is
expected to report only `initial-upload-closes-no-bugs`.

For the first Debian archive upload, use `dch` to replace the placeholder
with `YYYY.MM.N-1`, add `(Closes: #ITP_NUMBER)`, and finalize the entry for
`unstable`. Subsequent Debian-only revisions use `-2`, `-3`, and so on.
These archive changes are intentionally independent of the temporary GitHub
`-0losles1~...` entries.

```sh
DEBFULLNAME="Piotr T. Różański" \
DEBEMAIL="piotr@develancer.pl" \
  dch --newversion YYYY.MM.N-1 \
    "Initial release. (Closes: #ITP_NUMBER)"

DEBFULLNAME="Piotr T. Różański" \
DEBEMAIL="piotr@develancer.pl" \
  dch --release --distribution unstable
```

Set `G_MESSAGES_DEBUG=losles` when diagnosing monitor/colord selection. The
color manager logs connector/device names, lookup failures, and the selected
profile on either platform.

## Repository layout

```text
.
├── AGENTS.md                    This technical handoff
├── README.md                    User-facing capabilities and build guide
├── COPYING                      MIT license
├── Makefile                     Build, install, test, and sanitizer rules
├── .gitattributes               Cross-platform LF and binary-file policy
├── .github/workflows/
│   └── tagged-build.yml         Tag-only Linux/Windows release CI
├── tools/
│   ├── version.sh               CalVer validation and Git version derivation
│   └── copy-pe-runtime.sh       Recursive Windows runtime-DLL collection
├── packaging/windows/
│   ├── COPYING.rtf             Windows installer license rendering
│   ├── losles.rc                Native executable icon resource
│   └── losles.nsi               Per-user NSIS installer definition
├── data/
│   ├── icons/hicolor/512x512/apps/
│   │   └── io.github.develancer.losles.png
│   ├── icons/windows/
│   │   └── io.github.develancer.losles.ico
│   ├── io.github.develancer.losles.desktop
│   ├── io.github.develancer.losles.metainfo.xml
│   └── losles.1                 Command-line manual page
├── debian/
│   ├── changelog                Package version/upload history
│   ├── control                  Source/binary metadata and dependencies
│   ├── copyright                DEP-5 file licensing
│   ├── docs                     Install README with the binary package
│   ├── rules                    debhelper bridge to GNU Make
│   ├── source/format            3.0 (quilt) source-package format
│   ├── upstream/metadata        Upstream repository and bug tracker
│   └── watch                    Bare-CalVer GitHub tag discovery
├── src/
│   ├── main.c                   Locale setup and GApplication entry point
│   ├── losles-config.h          App ID, name, generated version include, URL
│   ├── losles-application.[ch]  Single-window app, open handling, shortcuts
│   ├── losles-cache-policy.[ch] Cache ordering, admission, and eviction
│   ├── losles-window.[ch]       UI, directory scan, jobs, caches, editing flow
│   ├── losles-image.[ch]        Immutable decoded-source model
│   ├── losles-platform.h        OS integration boundary
│   ├── losles-platform-linux.c  sysinfo, GIO Trash, links, permissions
│   ├── losles-platform-win32.c  RAM, Recycle Bin, links, portable icon path
│   ├── losles-rendered-image.[ch]
│   │                             Oriented, display-RGB pixel result/texture
│   ├── losles-color-manager.[ch]
│   │                             platform ICC lookup and LittleCMS rendering
│   └── formats/
│       ├── losles-format.[ch]   GObject interface for decoding/editing
│       ├── losles-format-registry.[ch]
│       │                         Module registration and format dispatch
│       ├── losles-jpeg-format.[ch]
│       │                         JPEG decode, ICC, TurboJPEG, Trash transaction
│       ├── losles-jpeg-metadata.[ch]
│       │                         Minimal EXIF orientation reader/writer
│       └── losles-png-format.[ch]
│                                 PNG decode, iCCP, capability checks, editing
└── tests/
    ├── test-cache-policy.c      Cache order and admission regression tests
    ├── test-jpeg-metadata.c     Endian/orientation parser tests
    └── test-formats.c           Decode, ICC, transform, Trash, invalid data
```

The `io.github.develancer.losles` name is the current application ID and
therefore appears in C, the desktop filename/content, and AppStream metadata.
It is derived from the upstream GitHub account. If the ID changes, update all
references together and rename both installed metadata files and the icon.
The application icon is a checked-in 512×512 PNG named after this ID in the
standard hicolor `apps` directory. The desktop entry uses the same
unqualified icon name so GNOME can resolve the installed asset. The Windows
ICO is a multi-resolution derivative of the same icon and must be regenerated
when the source PNG changes. Its entries are 256, 128, 64, 48, 32, 24, and
16 pixels. ImageMagick can reproduce it with:

```sh
convert data/icons/hicolor/512x512/apps/io.github.develancer.losles.png \
  -define icon:auto-resize=256,128,64,48,32,24,16 \
  data/icons/windows/io.github.develancer.losles.ico
```

## End-to-end architecture

The viewing pipeline is:

```text
GFile
  -> whole encoded file loaded by LoslesFormatRegistry
  -> magic-byte match by a LoslesFormat implementation
  -> format-specific decode into LoslesImage
  -> source ICC (or fallback) + display ICC resolved by LoslesColorManager
  -> LittleCMS conversion and EXIF orientation into LoslesRenderedImage
  -> GdkMemoryTexture
  -> GtkPicture
```

The editing pipeline is:

```text
LoslesImage + requested transform
  -> format-specific JPEG coefficient transform or PNG sample transform
     in a worker
  -> transformed temporary file with format metadata retained
  -> rotation: overwrite original path, without a Trash entry
  -> orientation normalization: rewrite the existing tag to 1, then overwrite
     the original path without a Trash entry
  -> crop: safety backup, original to Trash, replacement at original path
  -> result reopened and its directory rescanned
```

`LoslesImage` stores decoded pixels in encoded-file orientation, the embedded
ICC bytes, effective EXIF orientation, whether a valid EXIF orientation tag
was actually present, per-image rotation/crop capability flags, JPEG MCU
dimensions, a format label, and a strong reference to the format object which
created it. Keep it format-neutral. The presence flag is necessary because
absent orientation and a stored value of `1` have the same effective rendering
but different UI semantics.

`LoslesRenderedImage` stores pixels after color conversion and orientation.
Its output is RGB8 or RGBA8 even when the source is gray. It also records the
display profile's name/ID and whether the embedded source profile was actually
usable. `GdkMemoryTexture` retains the pixel bytes; the cache intentionally
retains both the rendered description and texture.

## Format-module contract

`LoslesFormat` is a GObject interface with:

- name and encoded-byte matching;
- decode to `LoslesImage`;
- capability queries for lossless rotation, orientation normalization, and
  crop;
- crop adjustment;
- lossless rotation, orientation-normalization, and crop execution.

The registry reads the complete `GFile` into memory once, then asks modules to
match the encoded signature in registration order. Directory discovery is
different: it uses filename suffixes only. A supported extension can therefore
appear in navigation and later fail magic/decode validation; a valid image
with an unknown extension can be opened directly but is not discovered as a
neighbor.

Format-interface capability queries describe whether a module implements an
operation at all. `LoslesImage` separately carries whether the particular
encoded file is eligible. `losles-window.c` requires both before enabling
Rotate or Crop. Keep per-file encoding checks in the format module; do not add
PNG or JPEG name checks to the window.

When adding a format:

1. Implement a stateless or thread-safe `LoslesFormat` GObject under
   `src/formats/`. One registry and its module objects can be used by concurrent
   decode workers.
2. Match file contents, not merely the filename.
3. Decode to one of `G8`, `GA8`, `RGB8`, or `RGBA8`, retain the raw embedded ICC
   bytes when applicable, and validate all dimension/stride arithmetic.
4. Honor `GCancellable`, particularly between rows or other bounded work
   units.
5. Register the object in `losles-format-registry.c`.
6. Add its files to `Makefile` and extensions to
   `losles_format_registry_supports_file()`.
7. Add decoder, malformed-input, ICC, alpha, and orientation tests as relevant.
8. Update desktop MIME types, AppStream metadata, `README.md`, and this file.
9. Expose editing callbacks only if their implementation preserves the
   project's definition of lossless and relevant metadata.

Do not put display-profile lookup or monitor-specific conversion in a format
module. Modules describe source pixels; `LoslesColorManager` owns conversion
to the output device.

### JPEG module details

- Matching requires the JPEG SOI prefix.
- libjpeg saves APP1 and APP2 markers before decoding.
- APP2 `ICC_PROFILE` segments are validated for sequence/count consistency and
  concatenated in sequence order.
- APP1 EXIF parsing is deliberately narrow: it finds the TIFF IFD0 orientation
  tag in either byte order and separately reports whether a valid stored tag
  was present. Missing/malformed orientation becomes effective value `1`.
- Grayscale decodes to `G8`; other supported JPEG data decodes to `RGB8`.
- CMYK and YCCK are rejected rather than converted incorrectly.
- Fancy chroma upsampling is enabled.
- MCU size is derived from sampling factors and retained for crop alignment.

JPEG edits call TurboJPEG's public `tjTransform()` API in the worker thread.
The transform uses `TJXOPT_PERFECT` and does not set `TJXOPT_COPYNONE`, so
TurboJPEG copies extra markers including ICC and EXIF. Output first goes to a
`.losles-output-XXXXXX` temporary file in the destination directory.

Ordinary rotation retains the EXIF orientation tag. Requested visual rotations
commute with orientations `1`, `3`, `6`, and `8`, so their raw coefficient
direction is unchanged. Orientations `2`, `4`, `5`, and `7` contain a
reflection; conjugating a rotation through a reflection reverses it, so a
visual right turn uses `TJXOP_ROT270` and a visual left turn uses
`TJXOP_ROT90`. `TJXOPT_PERFECT` means a rotation that would require trimming
incomplete edge blocks fails. A supported right rotation followed by a left
rotation must reproduce the complete encoded file byte for byte for all eight
orientations; keep the regression tests for this invariant.

The explicit orientation-normalization operation is available only when a
valid stored EXIF orientation tag exists and has value `2`–`8`. It maps those
values to the corresponding TurboJPEG symmetry operation (`HFLIP`, `ROT180`,
`VFLIP`, `TRANSPOSE`, `ROT90`, `TRANSVERSE`, or `ROT270`), then rewrites only
the copied orientation value to `1`. It leaves the displayed image unchanged.
Normalization uses the same direct-overwrite/no-Trash commit semantics as
ordinary rotation. Never remove the tag or synthesize one for an image that
does not have it.

The UI passes the source itself as the destination for both editing actions.
In-place editing requires a regular local file. Rotation completes the
transform before touching the source, then installs it with overwrite
semantics. Rotation intentionally creates no Trash entry or persistent backup;
failure before replacement leaves the source unchanged.

Crop additionally requires a functioning platform Trash implementation. It
creates a same-directory hard-link safety backup (falling back to a
metadata-preserving copy), moves the exact original with the shared platform
helper, and then moves the cropped file into the original path. Linux uses
GIO Trash; Windows uses `IFileOperation` with `FOFX_RECYCLEONDELETE`.
Cancellation is deliberately ignored during this final crop commit phase. If
installation fails after trashing, losles tries to restore the safety backup;
the error identifies the backup path if automatic restoration also fails.
Never replace the crop sequence with unlinking or direct truncation.

Crop is enabled only for orientation `1`. A requested rectangle is expanded
outward to MCU boundaries and clipped to image edges. The window calls the
format module's `adjust_crop` method during interaction, not just when the edit
starts, so the visible rectangle already shows that expansion. Do not bypass
the format method in the UI, change the operation to lossy pixel cropping, or
silently discard edge pixels.

TurboJPEG preserves JPEG COM and APP markers. Except for the explicit
orientation-value rewrite during normalization, losles does not selectively
rewrite them, so ICC, EXIF, XMP, IPTC, comments, maker-specific metadata, and
unknown APP markers remain. This means semantic dimension fields and embedded
thumbnails are retained but are not regenerated after a crop, quarter-turn
rotation, or normalization; they can describe the pre-edit image. Treat that
as a known metadata-consistency limitation, not permission to drop those
fields.

### PNG module details

- Matching uses the eight-byte PNG signature.
- The decoder supports grayscale, grayscale-alpha, RGB, RGBA, palette
  expansion, low-bit-depth grayscale expansion, `tRNS` alpha, and interlacing.
- `iCCP` data is copied into the source model before libpng teardown.
- 16-bit input is currently stripped to 8 bits.
- PNG EXIF orientation is not read.
- PNG `sRGB`, `gAMA`, and `cHRM` chunks are not synthesized into a source
  profile. Without `iCCP`, RGB is assumed sRGB and gray is assumed D65
  gamma 2.2 by the color manager.
- Rotation and crop are enabled only when the encoded source is static,
  non-interlaced, bit depth 8, and one of grayscale, grayscale-alpha, RGB, or
  RGBA. Palette color, grayscale depths 1/2/4, depth 16, Adam7 interlacing,
  and files containing `acTL`, `fcTL`, or `fdAT` stay view-only.
- The capability is recorded on `LoslesImage` during load and is revalidated
  against the current file immediately before an edit, because the file can
  change on disk while open.
- Editing decodes the original samples without the viewer's palette,
  precision, or transparency expansions. It rotates or subsets 1–4 byte
  pixels, then libpng recompresses the unchanged 8-bit sample values.
- The container writer replaces only `IHDR` and all `IDAT` chunks. It copies
  every other original chunk, its CRC, its relative position, and any bytes
  trailing `IEND` unchanged. This deliberately retains `iCCP`, `eXIf`, text,
  `pHYs`, and unknown ancillary metadata. Dimension-dependent metadata is not
  regenerated and can therefore describe the pre-edit image.
- PNG crop has no block-alignment constraint; `adjust_crop` clips the requested
  right/bottom edges to the image and otherwise preserves exact pixel
  coordinates.
- Rotation uses the same overwrite/no-Trash policy as JPEG. In-place crop uses
  the same exact-original-to-Trash and safety-backup policy.

JPEG and PNG currently have format-local implementations of the temporary
file/install transaction. Their safety behavior must remain equivalent:
changes to regular-file/symlink checks, permissions, cancellation, Trash,
backup, restore, or cleanup logic need review and tests for both modules.

## ICC and display-color model

Ubuntu 24.04's GTK 4.14 cannot attach an ICC colorspace to a surface. losles
therefore uses application-managed display conversion. Its Linux target
lookup is:

1. Find the `GdkMonitor` containing the window surface.
2. Read its connector with `gdk_monitor_get_connector()`.
3. Find a colord display device whose `XRANDR_name` metadata matches.
4. Connect to the device and read its default/selected profile file.
5. Require an RGB output profile.
6. Convert source samples directly to that profile with LittleCMS.
7. Pass already-converted, untagged device-RGB bytes to GTK.

This avoids a Mutter-specific protocol and is the intentional Ubuntu 24.04 SDR
path. VCGT calibration curves remain the desktop color service's job and must
not be applied again in losles.

The LittleCMS transform uses perceptual intent, black-point compensation, and
alpha copying. Orientation is applied while transformed rows are copied into
the output buffer. An embedded source profile is used only if LittleCMS can
open it and its color space matches the decoded pixel model: gray for
`G8`/`GA8`, RGB for `RGB8`/`RGBA8`. Invalid or incompatible profiles fall back
to D65 gamma-2.2 gray or sRGB respectively. The active output target falls
back to a generated sRGB profile when platform lookup/profile loading fails.

On Windows, `LoslesColorManager` receives both the `GdkMonitor` and the window
surface. `GDK_SURFACE_HWND()` supplies the native window, `MonitorFromWindow`
and `GetMonitorInfoW` supply its display device, and `GetICMProfileW` returns
the current output profile path for that device context. The manager caches
one target keyed by connector, profile path, mtime, and size, rechecking these
whenever rendering starts. Windows currently has no installed event hook for
a profile change while the same image remains idle; navigation or monitor
movement rechecks it. A Win32 target ID contains a SHA-256 digest of the
profile bytes. `LoslesWindow::render_profile_id` records the target used by
the current render generation. `start_render_for_image()` obtains the target
before accepting a cache or in-flight hit; an ID mismatch cancels in-flight
work, invalidates the whole render cache, and recomputes instead of displaying
old device-RGB pixels.

On Linux, the manager caches one target for the active connector and listens
to the active colord device's `changed` signal. The window watches monitor
enter/leave signals on both platforms. Monitor movement invalidates every
rendered result and starts a new foreground render.

This is not HDR support. It also assumes the Ubuntu 24.04 desktop will present
the supplied device-RGB values without applying a second per-surface ICC
transform. If a future compositor starts color-transforming every surface,
add a separate tagged-surface backend and select it explicitly; reusing this
preconversion path would double-transform colors.

## Window, asynchronous jobs, and caches

`LoslesWindow` intentionally centralizes orchestration. GTK objects and widget
state are touched only on the main thread. Directory scans, decodes, color
conversion, and edits run through `GTask` workers.

Directory/navigation behavior:

- Opening a file immediately starts its foreground decode.
- A background scan enumerates regular files in its parent directory.
- Only `.jpg`, `.jpeg`, `.jpe`, and `.png` neighbors are collected.
- Results are locale-aware sorted by basename, then the opened file's index is
  restored.
- If an application open request contains several files, only the first is
  used.
- There is no directory or file-change monitor; external edits do not
  invalidate cached content.

Two independent caches are keyed by the file URI:

- decoded `LoslesImage` cache: 10% of total system memory;
- display-profile `LoslesRenderedImage`/texture cache: 10% of total system
  memory.

`losles_window_init()` asks the platform layer for total physical memory once
per window and gives both caches the resulting independent 10% soft limit.
Linux uses `sysinfo().totalram` with its `mem_unit`; Windows uses
`GlobalMemoryStatusEx().ullTotalPhys`. Overflow is clamped to `G_MAXSIZE`. If
the platform query fails or reports zero memory, each cache falls back to
512 MiB.
`losles_cache_policy_limit_for_memory()` owns the tested percentage and
clamping arithmetic; keep operating-system discovery out of the policy helper.

Each cache is eligible to retain the current image and up to five neighbors on
either side, for at most eleven entries per cache before memory pressure is
considered. At most two background decodes and two background renders are in
flight. Foreground work may bypass those background concurrency checks.

Preloading visits neighbors nearest-first. At each distance it visits the
current navigation direction first; forward is the default after opening,
deleting, or resetting a browsing session. Memory pruning uses the inverse
priority: farthest-first, with the side opposite the navigation direction
evicted first at equal distance. Entries outside the eligible window are
always removed before applying the byte limit. The current image is protected,
and a foreground image may exceed its cache's nominal limit after lower
priority neighbors have been removed.

Background admission is a two-pass operation. It first checks whether enough
lower-priority entries exist to fit the candidate, then performs those
evictions. If the candidate cannot fit, it is rejected without flushing useful
entries. Decode/render failures are recorded separately from memory-capacity
rejections. Capacity-rejected or memory-evicted URIs are suppressed only for
the current navigation position; moving again clears that suppression so they
can be reconsidered under the new window and direction. A foreground request
always retries either kind of suppressed URI.

The size limits account primarily for pixel `GBytes`. They do not fully model
the transient whole encoded file, object/hash overhead, GTK texture resources,
or possible graphics-driver copies. Actual process memory can therefore exceed
20% of total system memory. A foreground image is allowed even if it exceeds a
nominal cache limit; neighbors are pruned around it.

Async correctness relies on these rules:

- `generation` identifies an opened file/directory set. Opening another file
  cancels the old load cancellable, clears load state, and makes old callbacks
  no-ops.
- Navigation inside the same scanned directory does not increment generation.
  A prefetched job can become foreground, so completion callbacks determine
  foreground status from the current file rather than whether it was
  originally queued as background work.
- `render_generation` identifies the active display target. Monitor/profile
  changes and opening a new file cancel render work and invalidate the entire
  URI-keyed render cache.
- `inflight` tables prevent duplicate work. `decode_failed` and
  `render_failed` suppress repeated background attempts after actual worker
  errors. `decode_capacity_blocked` and `render_capacity_blocked` prevent
  same-position retry loops after a cache-capacity rejection or eviction.
- Worker job structs own strong references to every object they use. Preserve
  these ownership rules.
- Always check generation before mutating an inflight/cache table in a
  completion callback. Stale callbacks refer to state already cleared for a
  newer generation.
- Cancellation is cooperative. New decoders/render loops must check it at
  bounded intervals.

Changing cache keys requires special care. The rendered cache is keyed only by
URI because every entry is globally discarded when the output profile
changes. If partial/profile-specific invalidation is introduced, the target
profile identity must become part of the key.

## UI behavior and coordinate assumptions

The UI is built directly in `losles-window.c`; there is no `.ui` resource.
It consists of a header bar and one `GtkOverlay`. The overlay's main child is
a canvas `GtkDrawingArea`; a clipped `GtkFixed` overlay positions and sizes
the `GtkPicture`, followed by the crop drawing layer, spinner, and bottom-left
information OSD.
The information OSD is hidden by default and does not participate in the
content layout. Its widget has no external margin: its opaque `#000000`
background begins at the exact bottom-left corner, with only internal text
padding. Both the window content and canvas have application-priority CSS
forcing a pure-black background regardless of the system light/dark
preference, including in fullscreen. Keep those explicit CSS classes if the
widget hierarchy changes; relying on theme defaults violates the intended
photo-viewing background.

Current actions and shortcuts:

- Open: `Ctrl+O`;
- Previous image: `Left`;
- Next image: `Right`;
- previous/next image: mouse Back/Forward thumb buttons;
- toggle the information OSD: information header button or `I`;
- enter/leave fullscreen: double-click the picture or `Alt+Enter`;
- enter/leave crop mode: crop header button or `C`;
- apply a valid crop selection: Crop header button or `Enter`;
- zoom at the pointer: mouse wheel over the image;
- pan a zoomed image: primary-button drag;
- cancel active crop mode, or reset zoom when Crop is inactive: `Escape`;
- move the current image to Trash and advance: `Delete`;
- open a dropped file: drag a Nautilus-style `GdkFileList` anywhere onto the
  window; the first file enters the normal open/directory-scan pipeline;
- lossless rotate left/right in place: header-bar buttons; the transformed
  file overwrites the source without creating a Trash entry;
- normalize orientation: a header-bar `dialog-warning-symbolic` icon button,
  enabled only for a valid stored EXIF orientation value `2`–`8`; its dynamic
  tooltip states whether a non-default EXIF orientation is present. It bakes
  the current display orientation into JPEG coefficients, sets the tag to
  `1`, and overwrites without creating a Trash entry;
- lossless crop in place: toggle, drag a new selection, move it from its
  interior, or resize it from any edge/corner, then Crop; the original goes to
  Trash before the cropped replacement is installed;
- About: a header-bar `dialog-question-symbolic` button opens a modal GTK
  About dialog with the application icon, version, copyright holder, MIT
  license, and source-repository link. Creator and license text deliberately
  lives in the main comments/copyright fields; do not set GTK's `authors` or
  `license-type` properties unless separate Credits/License pages are wanted
  again.

The crop toggle deliberately uses a 16×16 Cairo-drawn crop-corner glyph
instead of the theme name `crop-symbolic`. Yaru provides that nonstandard
name, but Adwaita—and therefore the Windows bundle—does not. The drawn glyph
inherits the widget's current GTK foreground color and disabled styling; do
not replace it with another desktop-theme-only name.

The centered loading spinner has a fixed 64×64-pixel request and an explicit
white application-priority color. On navigation, reload, or a new open
request, the previous `current_texture` remains attached to `GtkPicture` while
the selected file is decoded and color-converted. `current_image` is cleared
until the new decode completes, `foreground_loading` keeps edit controls and
destructive shortcuts disabled through rendering, and
`reset_zoom_on_next_display` defers the zoom decision until the replacement
texture is installed. Previous/next navigation also sets
`preserve_zoom_if_same_dimensions`; `display_rendered_image()` compares the
retained texture's displayed width and height with the replacement and keeps
`zoom_scale` plus the normalized center only for an exact match. Differently
sized images, direct opens, and edit reloads reset to fit. Thus the old image
and its current zoom remain visible under the spinner without allowing an
operation to target a file other than the one shown. The first image still
loads over the black canvas because there is no texture to retain. Foreground
decode/render errors clear the retained display rather than leaving a stale
image associated with the failed filename. Before starting the spinner,
`show_index()` checks both the source and rendered caches for the selected
URI. If both entries exist, it installs the cached image and texture
synchronously and never enters the visible loading state. A partial cache hit
still uses the spinner because a decode or display-profile conversion remains
to be completed.

Fullscreen hides the header bar and its controls. Keep
`notify::fullscreened` as the source of truth so the header is restored even
when the window manager changes fullscreen state outside the application
action. The double-click gesture is attached to the content overlay so it
continues to work over the picture and crop layer. Crop and apply-crop are
window actions rather than visibility-dependent button handling, so `C` and
`Enter` continue to work while the header is hidden. `Escape` never changes
fullscreen state: it leaves Crop mode when active and otherwise resets a
zoomed image. Fullscreen is left only by double-click or `Alt+Enter`.

The fitted image is zoom level `1.0`; wheel steps multiply or divide it by
`1.25`, clamped to `1.0`–`16.0`. `zoom_center_x/y` store the normalized image
point at the viewport center. A wheel event first derives the normalized image
point under the pointer, changes scale, then updates the center so that point
retains its canvas position. Placement is clamped only when an image edge
would expose space beyond that edge. Dragging changes the normalized center
only on axes where the scaled image is larger than the viewport.

`GtkPicture` displays the already color-managed `GdkTexture`; zoom only changes
the picture widget's size and position inside the clipped `GtkFixed`. It must
not trigger a new decode, ICC transform, or scaled-pixel cache. The canvas
`GtkDrawingArea::resize` signal updates the viewport dimensions; resizing or
entering fullscreen keeps the normalized image center. Previous/next
navigation keeps the scale and center when the incoming texture has exactly
the same width and height as the retained texture; otherwise the incoming
image resets to fit. Direct file opens and post-edit reloads always reset.

A capture-phase `GtkEventControllerLegacy` on the window consumes auxiliary
Back/Forward button press and release events and calls the same
`previous_image()`/`next_image()` functions as the header controls and
keyboard actions. X11/Wayland use conventional button numbers 8/9. The Win32
path additionally accepts GTK's XBUTTON1/2 mapping as 4/5; wheel motion is a
separate scroll event and continues to control zoom. Keep this controller at
the window level so the buttons work over the canvas, overlays, and header,
and keep navigation policy in the shared functions rather than duplicating it
in the event callback.

The crop overlay maps between the contained picture rectangle and display
pixel coordinates. Crop is disabled unless EXIF orientation is `1`, so those
coordinates currently equal encoded JPEG or PNG coordinates. If oriented
cropping is implemented, a real display-to-source coordinate transform is
required; simply enabling the button will crop the wrong area for
orientations 2–8.

Entering Crop mode calls `reset_zoom()` before showing the crop overlay.
Wheel zoom and view panning are inactive in Crop mode, so the existing crop
geometry always describes the initial fitted image. If zoomed cropping is
implemented later, both hit testing and pointer-to-image mapping must be
updated together.

Crop interaction is a small state machine (`CropDragMode`). Hit testing is
performed in widget coordinates with a fixed screen-space tolerance so narrow
images and different zoom scales remain usable. The actual rectangle is kept
in display/source pixel coordinates. A drag can create a selection, move the
whole selection while clamping it inside the image, or resize any edge or
corner while enforcing a two-pixel minimum. The eight black-and-white handles
and directional pointer cursors communicate the active affordance.

Every new or resized rectangle is immediately passed through
`losles_format_adjust_crop()`, making the overlay identical to the rectangle
used by the writer. Moves use a one-pixel probe through the same method to find
the enclosing legal cell, choose its nearest boundary, and accept only a
rectangle for which adjustment is idempotent. This preserves selection size
during moves. A selection that includes a partial MCU block at the image's
right or bottom edge may consequently be pinned on that axis until the edge is
resized to a movable block-aligned size. Keep the application layer
format-agnostic: future formats must express their crop constraints through
`adjust_crop`, rather than adding format-name checks here.

The information OSD is part of the color-management contract: it reports
whether the source used embedded ICC or an assumption and identifies the
selected display target/fallback. Updating its text while hidden is
intentional; toggling it on must reveal the current state rather than a stale
snapshot.

The normalization icon is wrapped in a sensitive `GtkBox` which carries the
same dynamic tooltip as the button. GTK's default widget picking excludes an
insensitive button, so the wrapper is what makes the disabled-state “No
rotation is stored in EXIF” tooltip discoverable. Keep the wrapper and button
tooltip properties synchronized if this control changes.

The file drop target is attached to the `LoslesWindow`, rather than only the
picture overlay, so the whole window is a drop zone. It requests
`GDK_TYPE_FILE_LIST` with copy semantics and opens only the first dropped
`GFile`; format validation remains the registry's job. Its `accept` and `drop`
handlers both reject while Crop mode or another serialized operation is
active. Keep both checks: the early rejection gives correct drag feedback,
while the drop-time check protects against state changes during a drag. An
accepted drop captures the current event timestamp and queues
`focus_after_file_drop()` at idle priority. Deferring until the DnD session
has completed lets it present the window, request toplevel keyboard focus
with the user-interaction timestamp, and focus the focusable `zoom_view`
inside GTK. Keep the request's strong window reference and destroy notifier;
calling only the generic synchronous open/present path can leave keyboard
focus in Nautilus.

Delete is an explicit destructive action with recoverability through the
system Trash. It runs in a worker and uses `losles_platform_trash_file()`;
never replace it with unlinking. After Trash succeeds, the same worker
rescans the directory.
The completion callback prefers the successor known before deletion, falling
back to the first supported filename collating after the deleted filename.
This matters when the original directory scan had not finished before the
user pressed Delete. If no later filename remains but the rescan is nonempty,
it selects the final entry, which is the deleted last image's predecessor.
Only an empty or failed post-Trash rescan enters the no-picture state.

Successful deletion increments both load and render generations and cancels
inflight work before installing the fresh file list. Completed neighbor
caches are retained so the successor can still display quickly; only entries
for the deleted URI are removed. Stale callbacks must continue checking their
generation before touching these tables. `operation_in_progress` serializes
rotation, normalization, crop, and deletion; navigation and opening check it
and refuse to start while one of those operations is active.

## Application and installed metadata

`LoslesApplication` is a single-window `GtkApplication` using
`G_APPLICATION_HANDLES_OPEN`. `main.c` only initializes the locale and runs
the application.

Files in `data/` are installed desktop integration, not runtime source:

- `.desktop` makes losles appear in launchers and associates JPEG/PNG MIME
  types;
- `.metainfo.xml` supplies AppStream/software-center metadata;
- `icons/hicolor/512x512/apps/io.github.develancer.losles.png` is installed
  under the matching hicolor theme path;
- `losles.1` documents invocation, editing safety, shortcuts, and diagnostics
  for command-line manual readers.

`src/losles-config.h` is the canonical C-side location for the application
ID, display name, and repository URL. It includes the generated
`losles-version.h` for the build version. Keep the repository URL in the
config header and AppStream metadata synchronized.

## Release engineering and distribution packaging

There is one release workflow,
`.github/workflows/tagged-build.yml`. It is the source of all GitHub release
artifacts; ordinary branch pushes and pull requests do not produce packages.
Each published release contains:

- `losles_<version>-0losles1~ubuntu24.04.1_amd64.deb`;
- `losles_<version>-0losles1~ubuntu26.04.1_amd64.deb`;
- `losles_<version>-0losles1~debian13.1_amd64.deb`;
- `losles-<version>-windows-x86_64-setup.exe`;
- `SHA256SUMS`, covering the four installable files.

The `.deb` files are upstream convenience builds, not an APT repository and
not official Debian/Ubuntu archive packages. The Windows installer is a
per-user, unsigned NSIS package. Keep the release description and filename
patterns in `README.md` synchronized with the workflow.

### Tag-triggered workflow

The tagged-build GitHub Actions workflow is deliberately release-only: branch
pushes and pull requests do not trigger it. GitHub's tag filter selects the
`YYYY.MM.N` shape, and `tools/version.sh --from-tag` applies the stricter
month and nonzero-counter validation in a dedicated job before any platform
build installs dependencies. A fail-fast-disabled Linux matrix then builds
inside the official `ubuntu:24.04`, `ubuntu:26.04`, and `debian:13` containers
on a GitHub Ubuntu runner. In parallel, a separate `windows-2025` job uses
MSYS2 UCRT64; it remains separate because its runner, shell, dependencies,
payload assembly, and installer smoke test do not fit cleanly into the Linux
container matrix. Every platform verifies the clean Git-derived version
before compiling. Each Linux target also stages `prefix=/usr` as an
installation smoke test. Those staged Linux trees remain job-local: they are
not useful distribution archives and must not be uploaded unless a future
diagnostic need justifies them.
The container workspace is mounted with ownership inherited from the host
runner, so a step after checkout adds exactly `$GITHUB_WORKSPACE` to the
container user's global Git `safe.directory` list before running the version
script. Do not replace this with the unsafe wildcard value `*`.

Only after the clean-tag tree is staged does each matrix leg use `dch` to
replace the placeholder changelog with
`<tag>-0losles1~<target><revision>`. Delaying that temporary edit matters:
otherwise Git version derivation would see a dirty checkout and embed
`<tag>+dirty` in the staged binary. Each target then builds its native binary
package through `dpkg-buildpackage`, verifies both changelog and `.deb`
versions, inspects the package, runs Lintian with errors fatal, and uploads a
separately labelled `.deb` artifact with seven-day retention. These temporary
artifacts are the matrix-to-release handoff. Native container builds matter
because runtime package names and symbol-derived minimum dependencies can
differ even when shared-library SONAMEs match. Keep `fetch-depth: 0`, because
development-version derivation needs tag history. Keep `fail-fast: false` so
one target's packaging failure does not hide results from the others.

### Windows installer assembly

The tagged workflow's Windows job uses the official `msys2/setup-msys2`
action in UCRT64 mode, builds without colord, runs the portable tests, and
builds one unsigned NSIS installer. Git for Windows performs checkout, while
MSYS2 Git performs version derivation. `.gitattributes` therefore pins
repository text to LF, and the verification step disables file-mode and
automatic-line-ending comparisons locally before checking for tracked
changes. Do not replace this with an ignore rule: `tools/version.sh` uses
`git status --porcelain=v1 --untracked-files=no`, so untracked build output
cannot add `.dirty`. This status query also refreshes Git's stat cache; do not
replace it with a bare `git diff-index`, which can report false changes when
Git for Windows and MSYS2 Git inspect the same checkout. If the tree is
genuinely changed, the verification step prints the tracked status and diff
summary before failing.

The job's private staging tree places the executable and DLLs under `bin`, the
application and Adwaita icons under `share/icons`, GLib schemas under
`share/glib-2.0`, and GdkPixbuf loaders under `lib`; preserve that relative
layout because both GTK runtime discovery and the Win32 PNG-icon fallback
depend on it. Project and MSYS2 dependency license texts accompany the
installed binaries under `share/licenses`.

The Windows installer payload does not copy the whole UCRT64 `bin` directory.
`tools/copy-pe-runtime.sh` reads PE import tables with `objdump`, recursively
follows only DLLs found in `/ucrt64/bin`, and treats imports not found there as
Windows system libraries. Its roots are `losles.exe` plus the dynamically
loaded GdkPixbuf JPEG and PNG modules. It prints each copied DLL together with
the PE file that imported it, making unexpected branches auditable in the CI
log. GTK 4 parses the restricted symbolic SVG format used by Adwaita icons
itself; do not restore the general-purpose librsvg GdkPixbuf loader unless
losles gains an actual runtime requirement for arbitrary SVG images. Add
another explicit root when a future runtime feature loads a module that is not
present in a PE import table. The workflow regenerates `loaders.cache` for
just JPEG and PNG. It also derives package ownership for copied runtime files
with `pacman` and includes only those packages' available license texts rather
than licenses for unrelated build tools. The generated aggregate
`gschemas.compiled` has no single pacman owner and is deliberately omitted
from the license-owner query; GLib itself is already included through its
runtime DLL.

`packaging/windows/losles.nsi` installs the staged tree per user under
`%LOCALAPPDATA%\Programs\losles` with no elevation. It creates a Start Menu
shortcut and an Apps & Features uninstaller. Its HKCU
`RegisteredApplications`, Capabilities, ProgID, and `Applications\losles.exe`
entries expose `.jpg`, `.jpeg`, `.jpe`, and `.png` through “Open with” and
Windows default-app selection; they must never directly replace the user's
existing extension defaults. Uninstall removes the files, shortcut, and only
losles-owned registry keys. Before recursively removing the application-owned
installation tree, it requires the installer-created
`.io.github.develancer.losles-install-root` marker; do not weaken that guard.

The NSIS license page uses `packaging/windows/COPYING.rtf`, not the UTF-8
plain-text `COPYING` file. The RTF stays ASCII at the byte level, declares
Windows Central European code page 1250, and represents Polish characters
with RTF Unicode escapes. Each prose paragraph is one unwrapped source line;
`\par` alone controls semantic paragraph breaks while the installer performs
visual word wrapping. Keep its legal text synchronized with `COPYING` without
copying that file's terminal-oriented hard wrapping into the RTF.

NSIS uses solid LZMA compression and embeds the checked-in Windows ICO in the
installer and uninstaller. The Makefile independently compiles
`packaging/windows/losles.rc` with `windres` so Explorer, Start Menu
shortcuts, and file associations obtain the same icon from `losles.exe`.
Calendar-version components map directly to the four-word PE installer
version: `YYYY.MM.N` becomes `YYYY.MM.N.0`, with leading zeroes removed from
the numeric components. The tagged job refuses a release counter larger than
the PE format's 16-bit component limit rather than folding it to a colliding
value.

After packaging, the workflow silently installs into a temporary per-user
directory, checks the executable, icon payload, and Apps & Features registry
entry, then silently uninstalls and checks their removal. Only the resulting
`losles-<version>-windows-x86_64-setup.exe` is uploaded as the
Windows-to-release-job handoff. Keep
`archive: false` on `actions/upload-artifact@v7` so the single installer is
stored directly rather than wrapped in an artifact ZIP. The release job uses
`actions/download-artifact@v8` with that exact filename; v8 is required for
direct v7 artifacts. The staging tree is an implementation detail and is not
uploaded.

The released installer is unsigned. Do not claim that it avoids SmartScreen
warnings. A future release workflow should Authenticode-sign and timestamp
the application executable before packaging, then sign and timestamp the
final installer; signing credentials must be supplied through protected
secrets or a dedicated signing service, never committed.

### Release publication

After every Linux matrix leg and the Windows job succeed, the release job
downloads the three `.deb` artifacts and the direct `.exe` artifact into one
directory. It requires the exact three target-qualified amd64 package names
and exact CalVer Windows-installer name, then creates one `SHA256SUMS` over
all four. With job-local `contents: write` permission, it creates the tag's
GitHub Release with generated notes and those five files. A rerun updates an
existing release's assets with `--clobber`. Build and validation jobs retain
read-only repository permission. GitHub adds the tag's source ZIP and tarball
itself.

For future releases:

1. Update user-visible documentation only when behavior, supported targets,
   dependencies, installation, or packaging has changed; do not edit it just
   to substitute a new release number.
2. Ensure the worktree is clean and `make test` passes.
3. Create and push exactly one `YYYY.MM.N` tag on the intended release commit;
   do not prefix it with `v`.
4. Let the workflow create or update the GitHub Release. Do not hand-build or
   rename its platform packages.
5. Check that the release has the three `.deb` files, one `.exe`, and
   `SHA256SUMS`, then smoke-test installation on representative Linux and
   Windows systems.

The AppStream metadata intentionally has no explicit release list. Tags and
GitHub Releases are the upstream release history; do not duplicate every tag
in `data/io.github.develancer.losles.metainfo.xml`.

### Debian packaging policy

The Debian packaging is intentionally usable beyond CI. `debian/rules`
passes `prefix=/usr` because the installed icon path is compiled in, and
passes `VERSION=$(DEB_VERSION_UPSTREAM)` to the clean, build, test, and
install phases so source-package builds do not depend on `.git`. Supplying it
during clean is important because `dh_auto_clean` dry-runs candidate Make
targets before selecting one; a Git failure during that probe can otherwise
be mistaken for output from an existing `distclean` target. The install phase
passes an empty `SOURCE_ICON_FILE` so a distributable binary contains only
the installed `/usr` icon path, not the build machine's absolute source
checkout. The ordinary developer build retains its absolute source-icon
fallback so `build/losles` has an icon before installation. The packaging
enables all normal dpkg hardening. `debian/control` uses `Architecture:
linux-any` because that binary package describes the Linux desktop
integration, and recommends rather than depends on `colord` because the
viewer has an explicit sRGB fallback. The maintainer address is
`piotr@develancer.pl`. Add Salsa
`Vcs-Git`/`Vcs-Browser` fields only after the corresponding repository
exists.

The source tree and application icon are MIT-licensed. The AppStream XML is
CC0-1.0 and carries an SPDX declaration; this exception is recorded in both
top-level `COPYING` and `debian/copyright`. Do not change ownership or license
claims without confirmation from the project owner.

The Makefile embeds both source-tree and installed icon paths. The former
lets a directly run `build/losles` load its own toplevel and About-dialog icon
before installation; the latter keeps an installed binary independent of the
source tree. On platforms that ignore per-toplevel icons (notably some
Wayland paths), GNOME associates the window through the matching
`GtkApplication`/desktop ID and finds the installed hicolor icon. The Windows
platform helper adds a third path. It first checks the source-tree
`../data/icons/...` path relative to `build/losles.exe`, then the portable
`../share/icons/hicolor/512x512/apps/<application-id>.png` path relative to a
bundled or installed `bin/losles.exe`. This runtime PNG is distinct from the
native ICO resource: the PNG supplies the GTK window/About image, while the
ICO supplies the Explorer executable, shortcut, association, and installer
icons.

There is currently no GResource bundle, translation catalog, settings schema,
or preferences storage. `LOCALEDIR` is defined for the C target but currently
unused; strings are hard-coded English.

## Tests and verification expectations

`test-cache-policy` checks the 10%-of-memory calculation and address-size
clamping, boundary handling, direction-aware preload and eviction order,
early callback termination, priority-preserving admission, non-destructive
capacity rejection, out-of-window rejection, and the foreground soft limit.

`test-jpeg-metadata` checks little-endian, big-endian, stored value `1`,
absent EXIF orientation, and in-file tag rewriting. `test-formats` creates
fixtures at runtime and checks:

- JPEG/PNG embedded ICC extraction and conversion to a supplied test target;
- JPEG EXIF orientation dimensions;
- PNG alpha preservation;
- grayscale JPEG/profile handling;
- PNG edit capability gating across direct-color 8-bit, palette, low-bit,
  16-bit, interlaced, and animated fixtures;
- PNG rotation/crop pixel results and byte-preservation of ancillary chunks;
- PNG rotation with no Trash entry and PNG crop with the exact original in
  Trash;
- coefficient JPEG rotation and crop plus ICC preservation;
- in-place rotation overwrite with no system-Trash entry;
- in-place crop replacement and isolated system-Trash preservation;
- preservation of EXIF, XMP, IPTC, ICC, COM, and unknown APP marker metadata;
- byte-identical right-then-left rotation, including a non-default EXIF
  orientation;
- visually correct rotation and byte-identical right/left round trips for all
  eight EXIF orientation values, including mirrored values;
- normalization of values `2`–`8`, including unchanged displayed pixels,
  canonical dimensions, an orientation value of `1`, preserved marker
  metadata, and no Trash entry;
- rejection of unsafe in-place rotation through symbolic links;
- invalid JPEG/PNG rejection.

For ordinary C changes, at minimum run:

```sh
make test
```

On Linux, the `test-formats` recipe runs with a temporary `HOME` and `TMPDIR`
on the
same filesystem. This is required for its isolated GIO Trash assertions:
GLib compares a file's filesystem with the home directory's filesystem when
choosing the home Trash. GitHub job containers mount their normal
`/github/home` separately from `/tmp`, which would otherwise make temporary
fixtures look like files on a system mount where Trash is unavailable. Keep
the test home isolated and same-filesystem rather than weakening or skipping
the Trash assertions. On Windows the native test executable uses its normal
temporary directory and exercises the real Recycle Bin; exact contents of the
opaque system Recycle Bin are not asserted. The privileged symbolic-link test
is reported as skipped on Windows, while the transform and regular-file
checks remain compiled.

Run the sanitizer build for parser, memory-ownership, cache, cancellation, or
threading changes. Add regression tests for new formats, malformed metadata,
overflow boundaries, orientation mappings, and editing guarantees.

Automated tests do not validate real colord/Win32 display-profile discovery,
movement between physical monitors, actual wide-gamut appearance, Windows
Recycle Bin recovery, GTK interaction (including overlay placement,
shortcuts, auxiliary mouse buttons, zoom handoff, gestures, and fullscreen),
or directory navigation timing. Changes in those areas need manual
graphical-session checks on Ubuntu 24.04 and Windows as applicable. Never
claim display-color correctness based only on an sRGB-to-sRGB unit test.

## Coding conventions and sensitive areas

- Follow the existing two-space C formatting and GLib/GObject idioms.
- Prefer `g_autoptr`, `g_autofree`, and explicit ownership transfer where they
  make lifetime clear.
- Use `GError` domains/codes consistently and propagate cancellation.
- Continue checking multiplication/addition before image-buffer allocation.
- libjpeg and libpng use `setjmp`/`longjmp`; variables requiring cleanup after
  a jump need the same `volatile` care as the existing decoders.
- Never call GTK APIs from a worker thread.
- Keep module instances stateless unless concurrent access is designed and
  tested.
- Preserve alpha through color conversion.
- Treat encoded content and metadata as hostile input.
- Keep temporary editing output in the destination directory and remove it on
  every failure path, except for a safety backup explicitly retained after an
  unrecoverable replacement failure.
- Do not weaken `TJXOPT_PERFECT` merely to make more rotations succeed.
- Do not label a transform that reduces the source sample depth, changes
  sample values, or discards metadata as lossless. Recompressing PNG samples
  at their original precision is pixel-lossless; lossy JPEG pixels must never
  be decoded and re-encoded for an advertised lossless edit.

The Makefile carries `-Wno-pedantic` because GTK 4.14's public
`gdkdmabufformats.h` contains an extra top-level semicolon. It otherwise
enables `-Wall`, `-Wextra`, and `-Wformat=2`. Revisit the exception only when
the supported GTK baseline no longer needs it.

## Known constraints worth remembering

- The registry temporarily holds the complete encoded file in memory in
  addition to the decoded result. Very large compressed images can cause a
  substantial peak.
- Cache budgets are based on total system memory measured when the window is
  created; they do not adapt to changing memory pressure and there is no user
  preference.
- Display buffers are always 8-bit; wide-gamut ICC support does not imply
  high-bit-depth or HDR output.
- The Linux colord path depends on connector metadata matching
  `CD_DEVICE_METADATA_XRANDR_NAME`. A missing connector/device/profile produces
  the visible sRGB fallback rather than guessing.
- The Windows output-profile path is rechecked only when a render starts;
  changing the selected profile while one image remains idle does not
  immediately redraw it.
- CMYK/YCCK JPEGs fail explicitly.
- Invalid embedded profiles are ignored in favor of the documented source
  fallback; this is reported as an assumed profile in the information OSD.
- PNG 16-bit samples are reduced to 8-bit for viewing. PNG `sRGB`, `gAMA`, and
  `cHRM` color descriptions are not interpreted as source profiles.
- PNG editing intentionally excludes palette, sub-8-bit, 16-bit, Adam7, and
  animated files. PNG recompression is pixel-lossless but is not expected to
  reproduce identical `IDAT` bytes.
- JPEG marker metadata is retained byte-for-byte except for the intentional
  orientation-tag rewrite during normalization, but dimension-dependent
  metadata is not regenerated after coefficient edits.
- Viewing may use any `GFile` that GIO can load, but lossless JPEG/PNG editing
  requires local filesystem paths.
- There is no file watching, thumbnail view, recursive scanning, recent-files
  list, settings persistence, or plugin loading at runtime.

When resolving one of these constraints, update the relevant implementation,
tests, `README.md`, and this file together.
