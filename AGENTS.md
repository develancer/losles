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
- Editing uses Save As. Do not overwrite the source implicitly.
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
  `jpegtran`.
- Display color: selected/default colord profile for the window's current
  monitor, with a built-in sRGB output fallback.
- Navigation: the opened file plus supported regular files in its parent
  directory, sorted using `g_utf8_collate()`.

Important current exclusions include CMYK/YCCK JPEG display, PNG editing,
16-bit display buffers, HDR, animations, zoom/pan controls, and lossless
rotation writing for mirrored EXIF orientations.

## Toolchain and dependencies

Build system: Meson with Ninja. C standard: C17. Default build type:
`debugoptimized`.

Compile/link dependencies:

- GTK 4 (`gtk4 >= 4.10`; Ubuntu 24.04 currently supplies 4.14);
- GLib/GIO/GObject and GDK through GTK;
- LittleCMS 2 (`lcms2 >= 2.14`);
- colord (`colord >= 1.4.6`);
- libjpeg, normally libjpeg-turbo on Ubuntu;
- libpng (`libpng >= 1.6`).

Runtime integration:

- `jpegtran` from `libjpeg-turbo-progs` is required only for editing;
- a running colord service and a display device/profile association are needed
  for monitor-specific output; viewing still works with the built-in sRGB
  target when lookup fails;
- a graphical GTK session is needed to exercise the application itself.

Install the Ubuntu dependencies with:

```sh
sudo apt install \
  build-essential meson ninja-build \
  libgtk-4-dev liblcms2-dev libcolord-dev \
  libjpeg-dev libpng-dev libjpeg-turbo-progs
```

Normal build, test, and run:

```sh
meson setup build -Dtests=true
meson compile -C build
meson test -C build --print-errorlogs
./build/src/losles /path/to/photo.jpg
```

For an existing build directory, Meson regenerates automatically after build
file changes. Use `meson setup --reconfigure build` when an explicit
reconfigure is useful.

The existing sanitizer configuration was created with:

```sh
meson setup build-asan \
  -Dtests=true \
  -Db_sanitize=address,undefined \
  -Db_lundef=false
meson compile -C build-asan
meson test -C build-asan --print-errorlogs
```

LeakSanitizer aborts under some ptraced/containerized agent environments even
after every test assertion passes. In that specific environment, verify
AddressSanitizer/UBSan with:

```sh
ASAN_OPTIONS=detect_leaks=0 \
  meson test -C build-asan --print-errorlogs
```

This workaround disables only leak detection and is not a substitute for a
normal LeakSanitizer run on an unrestricted host.

Build directories and their generated files are ignored and must not be
committed. Installation uses the normal Meson prefix, `/usr/local` by default:

```sh
meson install -C build
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
├── meson.build                  Project, dependencies, global compiler policy
├── meson_options.txt            `tests` option
├── data/
│   ├── meson.build              Installs desktop and AppStream metadata
│   ├── io.github.losles.Losles.desktop
│   └── io.github.losles.Losles.metainfo.xml
├── src/
│   ├── meson.build              Application target and source list
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
│       │                         JPEG decoder, ICC extraction, jpegtran writer
│       ├── losles-jpeg-metadata.[ch]
│       │                         Minimal EXIF orientation reader/writer
│       └── losles-png-format.[ch]
│                                 PNG decoder and iCCP extraction
└── tests/
    ├── meson.build
    ├── test-jpeg-metadata.c     Endian/orientation parser tests
    └── test-formats.c           Decode, ICC render, invalid data, jpegtran
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
LoslesImage + requested transform + Save As destination
  -> format-specific lossless operation in a worker
  -> jpegtran writes a temporary file in the destination directory
  -> metadata orientation adjustment when required
  -> GFile move with overwrite semantics
  -> destination reopened and its directory rescanned
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
6. Add its files to `src/meson.build` and extensions to
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

JPEG edits invoke the Ubuntu `jpegtran` executable with `-copy all` and
`-perfect`. The output first goes to a `.losles-XXXXXX` temporary file in the
destination directory. It replaces the chosen destination only after a
successful operation.

Rotation combines the image's current non-mirrored EXIF orientation with the
requested visual left/right rotation, materializes the result in coefficients,
and resets an existing orientation tag to `1`. Orientations `2`, `4`, `5`, and
`7` render correctly but rotation writing refuses them. `-perfect` means a
rotation that would require trimming incomplete edge blocks fails.

Crop is enabled only for orientation `1`. A drawn rectangle is expanded
outward to MCU boundaries and clipped to image edges before Save As. This can
produce a larger rectangle than the user's exact selection. Do not change it
to lossy pixel cropping or silently discard edge pixels.

`-copy all` preserves JPEG markers, including ICC and EXIF, but Losles only
rewrites the IFD0 orientation value. It does not comprehensively update other
EXIF dimension fields, embedded thumbnails, or maker-specific metadata after
a rotation/crop. Treat that as a known metadata limitation.

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
It consists of a header bar, a contained `GtkPicture`, a crop drawing overlay,
a spinner, and a status line.

Current actions and shortcuts:

- Open: `Ctrl+O`;
- Previous image: `Left`;
- Next image: `Right`;
- lossless rotate left/right: header-bar buttons;
- lossless crop: toggle, drag selection, then Crop/Save As.

The crop overlay maps between the contained picture rectangle and display
pixel coordinates. Crop is disabled unless EXIF orientation is `1`, so those
coordinates currently equal encoded JPEG coordinates. If oriented cropping
is implemented, a real display-to-source coordinate transform is required;
simply enabling the button will crop the wrong area for orientations 2–8.

The status line is part of the color-management contract: it reports whether
the source used embedded ICC or an assumption and identifies the selected
display target/fallback.

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
- invalid JPEG/PNG rejection.

The jpegtran integration test skips when `jpegtran` is absent. A skipped test
does not validate editing.

For ordinary C changes, at minimum run:

```sh
meson compile -C build
meson test -C build --print-errorlogs
```

Run the sanitizer build for parser, memory-ownership, cache, cancellation, or
threading changes. Add regression tests for new formats, malformed metadata,
overflow boundaries, orientation mappings, and editing guarantees.

Automated tests do not validate real colord discovery, movement between
physical monitors, actual wide-gamut appearance, GTK interaction, directory
navigation timing, or Save As dialogs. Changes in those areas need a manual
Ubuntu 24.04 graphical-session check in addition to unit tests. Never claim
display-color correctness based only on an sRGB-to-sRGB unit test.

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
  every failure path.
- Do not weaken `jpegtran -perfect` merely to make more rotations succeed.
- Do not label an 8-bit decode/re-encode path as lossless.

The project disables GCC's pedantic warning globally because GTK 4.14's public
`gdkdmabufformats.h` contains an extra top-level semicolon. Other warning-level
3 diagnostics remain enabled. Revisit that exception only when the supported
GTK baseline no longer needs it.

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
  fallback; this is reported as an assumed profile in the status line.
- PNG 16-bit samples and non-iCCP color chunks are not preserved in the view
  pipeline.
- JPEG marker copying is broad, but metadata rewriting is intentionally
  minimal.
- Viewing may use any `GFile` that GIO can load, but lossless JPEG editing
  requires local filesystem paths.
- There is no file watching, thumbnail view, recursive scanning, recent-files
  list, settings persistence, or plugin loading at runtime.

When resolving one of these constraints, update the relevant implementation,
tests, `README.md`, and this file together.
