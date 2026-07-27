# AGENTS.md

This file is the technical handoff for AI agents working on Losles. It applies
to the entire repository. Read it together with `README.md` before changing
the project.

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

Losles is a small, responsive, color-managed photo viewer aimed at Ubuntu
24.04 and later. The current release is `0.1.0`, the code is MIT-licensed, and
the implementation is deliberately C17 with GTK 4 rather than a
runtime-heavy application stack.

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
  COM, or unrecognized APP markers. Do not normalize EXIF orientation when
  coefficient rotation can retain it.
- Image formats stay behind the `LoslesFormat` module interface so adding a
  decoder/editor does not require format-specific logic in the window or color
  manager.
- Navigation should favor low latency. Adjacent images are decoded and
  color-converted in advance, within bounded memory and concurrency limits.
- Prefer libraries shipped by the default Ubuntu repositories. Avoid vendored
  frameworks or a new language runtime without a concrete, measured reason.

## Supported behavior at a glance

- Viewing: 8-bit grayscale, grayscale-alpha, RGB, and RGBA JPEG/PNG paths.
- JPEG ICC: `ICC_PROFILE` APP2 chunks, including multi-marker profiles.
- PNG ICC: `iCCP`.
- Orientation: all eight JPEG EXIF orientation values are displayed.
- Editing: coefficient-level JPEG rotation and MCU-aligned JPEG crop through
  libturbojpeg.
- Display color: selected/default colord profile for the window's current
  monitor, with a built-in sRGB output fallback.
- Navigation: the opened file plus supported regular files in its parent
  directory, sorted using `g_utf8_collate()`.

Important current exclusions include CMYK/YCCK JPEG display, PNG editing,
16-bit display buffers, HDR, animations, zoom/pan controls, and lossless
rotation writing for mirrored EXIF orientations.

## Toolchain and dependencies

Build system: GNU Make. The Makefile defaults to `-O2 -g`, compiles as C17,
and keeps generated output outside the source tree.

Compile/link dependencies:

- GTK 4 (`gtk4 >= 4.10`; Ubuntu 24.04 currently supplies 4.14);
- GLib/GIO/GObject and GDK through GTK;
- LittleCMS 2 (`lcms2 >= 2.14`);
- colord (`colord >= 1.4.6`);
- libjpeg, normally libjpeg-turbo on Ubuntu;
- TurboJPEG, the public lossless-transform API from libjpeg-turbo;
- libpng (`libpng >= 1.6`).

Runtime integration:

- a running colord service and a display device/profile association are needed
  for monitor-specific output; viewing still works with the built-in sRGB
  target when lookup fails;
- a graphical GTK session is needed to exercise the application itself.

Install the Ubuntu dependencies with:

```sh
sudo apt install \
  build-essential pkg-config \
  libgtk-4-dev liblcms2-dev libcolord-dev \
  libjpeg-dev libturbojpeg0-dev libpng-dev
```

Normal build, test, and run:

```sh
make
make test
./build/losles /path/to/photo.jpg
```

The Makefile uses `pkg-config`, generates header dependency files, and keeps
all normal output in `build`. It supports `prefix`, `bindir`, `datadir`,
`applicationsdir`, `metainfodir`, `localedir`, `DESTDIR`, `BUILD_DIR`,
`CFLAGS`, `LDFLAGS`, `LDLIBS`, and `SANITIZE` overrides. Use a distinct
`BUILD_DIR` after changing compiler/sanitizer modes because Make does not
record command-line flag changes:

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

Build directories and their generated files are ignored and must not be
committed. Installation uses the normal Make prefix, `/usr/local` by default:

```sh
make install
```

For a non-privileged installation smoke test, use a temporary `DESTDIR`
instead of installing into the live system.

Set `G_MESSAGES_DEBUG=losles` when diagnosing monitor/colord selection. The
color manager logs connector names, lookup failures, and the selected profile.

## Repository layout

```text
.
├── AGENTS.md                    This technical handoff
├── README.md                    User-facing capabilities and build guide
├── COPYING                      MIT license
├── Makefile                     Build, install, test, and sanitizer rules
├── data/
│   ├── io.github.losles.Losles.desktop
│   └── io.github.losles.Losles.metainfo.xml
├── src/
│   ├── main.c                   Locale setup and GApplication entry point
│   ├── losles-application.[ch]  Single-window app, open handling, shortcuts
│   ├── losles-window.[ch]       UI, directory scan, jobs, caches, editing flow
│   ├── losles-image.[ch]        Immutable decoded-source model
│   ├── losles-rendered-image.[ch]
│   │                             Oriented, display-RGB pixel result/texture
│   ├── losles-color-manager.[ch]
│   │                             colord target lookup and LittleCMS rendering
│   └── formats/
│       ├── losles-format.[ch]   GObject interface for decoding/editing
│       ├── losles-format-registry.[ch]
│       │                         Module registration and format dispatch
│       ├── losles-jpeg-format.[ch]
│       │                         JPEG decode, ICC, TurboJPEG, Trash transaction
│       ├── losles-jpeg-metadata.[ch]
│       │                         Minimal EXIF orientation reader/writer
│       └── losles-png-format.[ch]
│                                 PNG decoder and iCCP extraction
└── tests/
    ├── test-jpeg-metadata.c     Endian/orientation parser tests
    └── test-formats.c           Decode, ICC, transform, Trash, invalid data
```

The `io.github.losles.Losles` name is the current application ID and therefore
appears in C, the desktop filename/content, and AppStream metadata. It is
provisional naming, not evidence of an existing GitHub organization. If the
ID changes, update all references together and rename both installed metadata
files. The desktop entry currently uses the system's generic
`image-x-generic` icon; there is no checked-in custom icon.

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
  -> format-specific TurboJPEG operation in a worker
  -> transformed temporary file with JPEG marker metadata retained
  -> rotation: overwrite original path, without a Trash entry
  -> crop: safety backup, original to Trash, replacement at original path
  -> result reopened and its directory rescanned
```

`LoslesImage` stores decoded pixels in encoded-file orientation, the embedded
ICC bytes, EXIF orientation, JPEG MCU dimensions, a format label, and a strong
reference to the format object which created it. Keep it format-neutral.

`LoslesRenderedImage` stores pixels after color conversion and orientation.
Its output is RGB8 or RGBA8 even when the source is gray. It also records the
display profile's name/ID and whether the embedded source profile was actually
usable. `GdkMemoryTexture` retains the pixel bytes; the cache intentionally
retains both the rendered description and texture.

## Format-module contract

`LoslesFormat` is a GObject interface with:

- name and encoded-byte matching;
- decode to `LoslesImage`;
- capability queries for lossless rotation/crop;
- crop adjustment;
- lossless rotation/crop execution.

The registry reads the complete `GFile` into memory once, then asks modules to
match the encoded signature in registration order. Directory discovery is
different: it uses filename suffixes only. A supported extension can therefore
appear in navigation and later fail magic/decode validation; a valid image
with an unknown extension can be opened directly but is not discovered as a
neighbor.

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
  tag in either byte order. Missing/malformed orientation becomes `1`.
- Grayscale decodes to `G8`; other supported JPEG data decodes to `RGB8`.
- CMYK and YCCK are rejected rather than converted incorrectly.
- Fancy chroma upsampling is enabled.
- MCU size is derived from sampling factors and retained for crop alignment.

JPEG edits call TurboJPEG's public `tjTransform()` API in the worker thread.
The transform uses `TJXOPT_PERFECT` and does not set `TJXOPT_COPYNONE`, so
TurboJPEG copies extra markers including ICC and EXIF. Output first goes to a
`.losles-output-XXXXXX` temporary file in the destination directory.

Rotation applies only the requested visual left/right coefficient rotation and
retains the existing non-mirrored EXIF orientation tag. Rotations commute with
orientations `1`, `3`, `6`, and `8`, so this produces the requested display
rotation without rewriting metadata. Orientations `2`, `4`, `5`, and `7`
render correctly but rotation writing refuses them. `TJXOPT_PERFECT` means a
rotation that would require trimming incomplete edge blocks fails. A supported
right rotation followed by a left rotation must reproduce the complete encoded
file byte for byte; keep the regression test for this invariant.

The UI passes the source itself as the destination for both editing actions.
In-place editing requires a regular local file. Rotation completes the
transform before touching the source, then installs it with overwrite
semantics. Rotation intentionally creates no Trash entry or persistent backup;
failure before replacement leaves the source unchanged.

Crop additionally requires a functioning GIO Trash implementation. It creates
a same-directory hard-link safety backup (falling back to a
metadata-preserving copy), moves the exact original with `g_file_trash()`, and
then moves the cropped file into the original path. Cancellation is
deliberately ignored during this final crop commit phase. If installation
fails after trashing, Losles tries to restore the safety backup; the error
identifies the backup path if automatic restoration also fails. Never replace
the crop sequence with unlinking or direct truncation.

Crop is enabled only for orientation `1`. A drawn rectangle is expanded
outward to MCU boundaries and clipped to image edges before the in-place edit.
This can produce a larger rectangle than the user's exact selection. Do not
change it to lossy pixel cropping or silently discard edge pixels.

TurboJPEG preserves JPEG COM and APP markers. Losles does not selectively
rewrite them, so ICC, EXIF (including orientation), XMP, IPTC, comments,
maker-specific metadata, and unknown APP markers remain. This means semantic
dimension fields and embedded thumbnails are retained but are not regenerated
after a crop; they can describe the pre-crop image. Treat that as a known
metadata-consistency limitation, not permission to drop those fields.

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
- Editing callbacks intentionally return unsupported. Pixel-lossless PNG
  recompression is not enough until sample depth and relevant ancillary
  metadata can be preserved deliberately.

## ICC and display-color model

Ubuntu 24.04's GTK 4.14 cannot attach an ICC colorspace to a surface. Losles
therefore uses application-managed display conversion:

1. Find the `GdkMonitor` containing the window surface.
2. Read its connector with `gdk_monitor_get_connector()`.
3. Find a colord display device whose `XRANDR_name` metadata matches.
4. Connect to the device and read its default/selected profile file.
5. Require an RGB output profile.
6. Convert source samples directly to that profile with LittleCMS.
7. Pass already-converted, untagged device-RGB bytes to GTK.

This avoids a Mutter-specific protocol and is the intentional Ubuntu 24.04 SDR
path. VCGT calibration curves remain the desktop color service's job and must
not be applied again in Losles.

The LittleCMS transform uses perceptual intent, black-point compensation, and
alpha copying. Orientation is applied while transformed rows are copied into
the output buffer. An embedded source profile is used only if LittleCMS can
open it and its color space matches the decoded pixel model: gray for
`G8`/`GA8`, RGB for `RGB8`/`RGBA8`. Invalid or incompatible profiles fall back
to D65 gamma-2.2 gray or sRGB respectively. The active output target falls
back to a generated sRGB profile when colord lookup/profile loading fails.

`LoslesColorManager` caches one target for the active connector. It listens to
the active colord device's `changed` signal. The window also watches monitor
enter/leave signals. Either event invalidates every rendered result and starts
a new foreground render.

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

- decoded `LoslesImage` cache: 512 MiB;
- display-profile `LoslesRenderedImage`/texture cache: 512 MiB.

Both retain only the current image and neighbors within distance two. At most
two background decodes and two background renders are in flight. Foreground
work may bypass those background concurrency checks. Preloading visits the
next image before the previous image at each distance.

The size limits account primarily for pixel `GBytes`. They do not fully model
the transient whole encoded file, object/hash overhead, GTK texture resources,
or possible graphics-driver copies. Actual process memory can therefore exceed
1 GiB. A foreground image is allowed even if it exceeds a nominal cache limit;
neighbors are pruned around it.

Async correctness relies on these rules:

- `generation` identifies an opened file/directory set. Opening another file
  cancels the old load cancellable, clears load state, and makes old callbacks
  no-ops.
- Navigation inside the same scanned directory does not increment generation.
  A prefetched job can become foreground, so completion callbacks determine
  foreground status from the current file rather than the job's initial flag.
- `render_generation` identifies the active display target. Monitor/profile
  changes and opening a new file cancel render work and invalidate the entire
  URI-keyed render cache.
- `inflight` tables prevent duplicate work. `failed`/`render_blocked` suppress
  repeated background attempts, but a foreground request retries.
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
It consists of a header bar and one `GtkOverlay` containing a contained
`GtkPicture`, crop drawing layer, spinner, and bottom-left information OSD.
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
- toggle the information OSD: information header button or `I`;
- enter/leave fullscreen: double-click the picture or `Alt+Enter`;
- leave fullscreen and/or cancel active crop mode: `Escape`;
- lossless rotate left/right in place: header-bar buttons; the transformed
  file overwrites the source without creating a Trash entry;
- lossless crop in place: toggle, drag a new selection, move it from its
  interior, or resize it from any edge/corner, then Crop; the original goes to
  Trash before the cropped replacement is installed.

Fullscreen hides the header bar and its controls. Keep
`notify::fullscreened` as the source of truth so the header is restored even
when the window manager changes fullscreen state outside the application
action. The double-click gesture is attached to the content overlay so it
continues to work over the picture and crop layer.

The crop overlay maps between the contained picture rectangle and display
pixel coordinates. Crop is disabled unless EXIF orientation is `1`, so those
coordinates currently equal encoded JPEG coordinates. If oriented cropping
is implemented, a real display-to-source coordinate transform is required;
simply enabling the button will crop the wrong area for orientations 2–8.

Crop interaction is a small state machine (`CropDragMode`). Hit testing is
performed in widget coordinates with a fixed screen-space tolerance so narrow
images and different zoom scales remain usable. The actual rectangle is kept
in display/source pixel coordinates. A drag can create a selection, move the
whole selection while clamping it inside the image, or resize any edge or
corner while enforcing a two-pixel minimum. The eight black-and-white handles
and directional pointer cursors communicate the active affordance. If crop
zooming or free rotation is added, update both hit testing and pointer-to-image
mapping together; never infer source coordinates directly from the window.

The information OSD is part of the color-management contract: it reports
whether the source used embedded ICC or an assumption and identifies the
selected display target/fallback. Updating its text while hidden is
intentional; toggling it on must reveal the current state rather than a stale
snapshot.

## Application and installed metadata

`LoslesApplication` is a single-window `GtkApplication` using
`G_APPLICATION_HANDLES_OPEN`. `main.c` only initializes the locale and runs
the application.

Files in `data/` are installed desktop integration, not runtime source:

- `.desktop` makes Losles appear in launchers and associates JPEG/PNG MIME
  types;
- `.metainfo.xml` supplies AppStream/software-center metadata.

There is currently no icon installation, GResource bundle, translation
catalog, settings schema, or preferences storage. `LOCALEDIR` is defined for
the C target but currently unused; strings are hard-coded English.

## Tests and verification expectations

`test-jpeg-metadata` checks little-endian, big-endian, and absent EXIF
orientation. `test-formats` creates fixtures at runtime and checks:

- JPEG/PNG embedded ICC extraction and conversion to a supplied test target;
- JPEG EXIF orientation dimensions;
- PNG alpha preservation;
- grayscale JPEG/profile handling;
- coefficient JPEG rotation and crop plus ICC preservation;
- in-place rotation overwrite with no system-Trash entry;
- in-place crop replacement and isolated system-Trash preservation;
- preservation of EXIF, XMP, IPTC, ICC, COM, and unknown APP marker metadata;
- byte-identical right-then-left rotation, including a non-default EXIF
  orientation;
- rejection of unsafe in-place rotation through symbolic links;
- invalid JPEG/PNG rejection.

For ordinary C changes, at minimum run:

```sh
make test
```

Run the sanitizer build for parser, memory-ownership, cache, cancellation, or
threading changes. Add regression tests for new formats, malformed metadata,
overflow boundaries, orientation mappings, and editing guarantees.

Automated tests do not validate real colord discovery, movement between
physical monitors, actual wide-gamut appearance, GTK interaction (including
overlay placement, shortcuts, gestures, and fullscreen), or directory
navigation timing. Changes in those areas need a manual Ubuntu 24.04
graphical-session check in addition to unit tests. Never claim display-color
correctness based only on an sRGB-to-sRGB unit test.

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
- Do not label an 8-bit decode/re-encode path as lossless.

The Makefile carries `-Wno-pedantic` because GTK 4.14's public
`gdkdmabufformats.h` contains an extra top-level semicolon. It otherwise
enables `-Wall`, `-Wextra`, and `-Wformat=2`. Revisit the exception only when
the supported GTK baseline no longer needs it.

## Known constraints worth remembering

- The registry temporarily holds the complete encoded file in memory in
  addition to the decoded result. Very large compressed images can cause a
  substantial peak.
- The cache budgets are compile-time constants in `losles-window.c`; there is
  no user preference or adaptive memory policy.
- Display buffers are always 8-bit; wide-gamut ICC support does not imply
  high-bit-depth or HDR output.
- The colord path depends on connector metadata matching
  `CD_DEVICE_METADATA_XRANDR_NAME`. A missing connector/device/profile produces
  the visible sRGB fallback rather than guessing.
- CMYK/YCCK JPEGs fail explicitly.
- Invalid embedded profiles are ignored in favor of the documented source
  fallback; this is reported as an assumed profile in the information OSD.
- PNG 16-bit samples and non-iCCP color chunks are not preserved in the view
  pipeline.
- JPEG marker metadata is retained byte-for-byte, but dimension-dependent
  metadata is not regenerated after a crop.
- Viewing may use any `GFile` that GIO can load, but lossless JPEG editing
  requires local filesystem paths.
- There is no file watching, thumbnail view, recursive scanning, recent-files
  list, settings persistence, or plugin loading at runtime.

When resolving one of these constraints, update the relevant implementation,
tests, `README.md`, and this file together.
