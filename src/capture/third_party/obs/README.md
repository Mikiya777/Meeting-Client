# OBS Dependency Notes

This directory records the local OBS dependency layout used by the capture
module during development on macOS.

## Current Layout

- `sdk/32.1.2/macos-arm64/`
  - Local fixed-version OBS SDK assembled for macOS arm64 development.
  - Headers come from the official OBS Studio 32.1.2 source package.
  - Runtime files come from the installed OBS.app 32.1.2 application.
  - This directory is ignored by git because it contains binary dependencies.

- `obs-studio/`
  - Optional local OBS Studio source tree downloaded from `obsproject/obs-studio`.
  - Used only for reading source and checking APIs.
  - This directory is ignored by git to avoid committing a large local source
    drop.

- `include/`, `bin/`, `data/`
  - Reserved for a future curated OBS SDK layout.
  - These are currently placeholders.

## SDK Contents

Current SDK root:

`src/capture/third_party/obs/sdk/32.1.2/macos-arm64`

It contains:

- `include/`
  - `obs.h`, related libobs headers, a local `obsconfig.h`, and `simde`
    headers required by OBS math/graphics headers.
- `Frameworks/`
  - `libobs.framework`
  - `libobs-metal.dylib`
  - `libobs-opengl.dylib`
  - FFmpeg libraries required by `libobs.framework`.
  - Secondary FFmpeg dependencies such as `librist`, `libsrt`, `libmbed*`,
    and `libx264`.
- `PlugIns/`
  - `mac-avcapture.plugin`
  - `mac-capture.plugin`
- `Resources/`
  - OBS runtime resources copied from OBS.app.

TODO(obs-sdk): The current SDK was assembled from official OBS 32.1.2 sources
and the installed OBS.app 32.1.2 runtime. For release packaging, rebuild or
extract all SDK files from the same official artifact and document checksums.

## Rebuild The Local SDK

1. Download and extract official `OBS-Studio-32.1.2-Sources`.
2. Install official OBS Studio 32.1.2 for macOS arm64.
3. Copy libobs headers from the source package into `sdk/32.1.2/macos-arm64/include`.
4. Copy `simde` headers into `sdk/32.1.2/macos-arm64/include/simde`.
   - Current local source: `/opt/homebrew/Cellar/simde/0.8.2/include/simde`
5. Copy required runtime files from `/Applications/OBS.app/Contents`:
   - `Frameworks/libobs.framework`
   - `Frameworks/libobs-metal.dylib`
   - `Frameworks/libobs-opengl.dylib`
   - required FFmpeg dylibs
   - required secondary FFmpeg dependency dylibs
   - `PlugIns/mac-avcapture.plugin`
   - `PlugIns/mac-capture.plugin`
   - `Resources`
6. Regenerate `include/obsconfig.h` if SDK paths or OBS version change.

## macOS Source IDs

Confirmed from the local OBS source tree:

- Camera source: `macos-avcapture`
- Fast camera source: `macos-avcapture-fast`
- Microphone source: `coreaudio_input_capture`

The Windows source IDs from reference documents do not apply on macOS:

- `dshow_input`
- `wasapi_input_capture`

## Version Note

The installed OBS.app currently reports version `32.1.2`. If the downloaded
OBS source tree is from a different branch or release, treat it as API
reference only. Avoid relying on mismatched source headers and runtime binaries
as a permanent project setup.
