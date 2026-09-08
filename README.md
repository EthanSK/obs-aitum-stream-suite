# Aitum++

My fork of [Aitum Stream Suite](https://github.com/Aitum/obs-aitum-stream-suite)
1.2.1 for extra OBS canvases, separate streaming outputs and recovery controls.

[My OBS setup](https://github.com/EthanSK/obs-plus-plus/blob/master/SETUP.md) ·
[Changes and fixes](FORK-CHANGES.md) ·
[Interactive room](https://ethansk.github.io/ethan-setup/)

## What I use it for

I keep the main recording and stream in OBS++, with separate Twitch and Kick
outputs in Aitum. Each output has its own encoder settings and live health
information. An extra canvas gives those streams a different layout without
changing the main recording.

- **Tools → Aitum++ Controls** shows the extra canvas and Outputs docks.
- **Tools → Aitum++ Workspaces (Advanced)** provides Live, Build and custom dock layouts. It opens by default after preserving the normal OBS layout; uncheck it to return to that layout for the session.
- **Tools → Aitum++ Settings** opens canvas/output configuration; the Outputs dock gear opens it too.
- The Outputs dock provides individual controls, grouped start/stop actions and bitrate, dropped-frame and congestion information. CPU is the shared OBS process total.

The fork preserves the selected theme, mixer orientation and context toolbars.
It does not turn the main preview into a dock unless `Aitum/MainCanvasDock=true`
is explicitly set. Existing `aitum.json` profile settings remain compatible.
Opening a workspace does not automatically start streaming or recording.

## Recovery and fixes

| Control or fix | What it does |
| --- | --- |
| Restart Screen Capture | Calls OBS's native restart action for macOS Screen Capture sources that OBS has marked restartable. |
| Restart Camera Services | Requests administrator permission to restart macOS camera services, then reapplies the existing OBS camera-source settings after two seconds. It can run while outputs continue; camera frames may briefly disappear and other camera apps can be affected. |
| Restart OBS | Hands the current profile, collection and scene to [OBScene](https://github.com/EthanSK/OBScene), which owns the restart and output-state checks. OBScene is a separate app. |
| Reconnecting outputs | Keeps the output alive while its retry thread runs, avoiding a release/use-after-free crash during reconnection. |
| Apple H.264 output dimensions | Aligns dimensions for extra canvases to prevent diagonally sheared frames; the observed 1670 × 1080 canvas becomes 1672 × 1080 at the encoder. |
| Preview boundary | Makes the canvas edge visible while arranging sources. |

See [FORK-CHANGES.md](FORK-CHANGES.md) for the implementation commits and limits.
These controls do not guarantee recovery from every hardware failure. The
website demonstrates the layout; it does not call recovery commands on your Mac.

## Build on macOS

**Source distribution:** this fork has no public installer or binary release.
Ethan uses a local Apple-silicon build with OBS++ 32.2.2. Upstream Aitum downloads
do not include this fork's changes.

Use full Xcode, Git and CMake 3.28+ on macOS. The preset downloads and builds the
OBS development libraries and fetches Qt/other dependencies pinned in
`buildspec.json`; their build-dependency version is separate from the installed
OBS++ runtime version. For Ethan's OBS++ build, use macOS 26+ and Xcode 26+ as
specified in the [OBS++ setup guide](https://github.com/EthanSK/obs-plus-plus/blob/master/SETUP.md).

```sh
git clone https://github.com/EthanSK/obs-aitum-stream-suite.git
cd obs-aitum-stream-suite
cmake --preset macos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCODESIGN_IDENTITY=-
cmake --build build_macos --config RelWithDebInfo --parallel 4
```

The plugin is `build_macos/RelWithDebInfo/aitum-stream-suite.plugin`. Once OBS
has stopped recording/streaming and is quit, install it into the per-user
plugin directory:

```sh
cmake --install build_macos --config RelWithDebInfo \
  --prefix "$HOME/Library/Application Support/obs-studio/plugins"
```

Back up an existing Aitum plugin before replacement and keep only one loaded
copy; both forks use the same plugin identity and configuration. Launch OBS++
and check its log for `loaded version 1.2.1 (Aitum++ fork)`, then make a short
local recording before configuring real streams. Use your own service keys;
raw profile files are not included.

The example uses ad-hoc signing, not notarization. The documented build/install
paths were checked against the presets, source and previous local build
artifacts; this documentation update did not perform a clean native build or
restart the installed plugin. Other operating systems and distribution
packaging are not verified by this personal macOS setup.

## Upstream and support

This is an independent fork, not an official Aitum release. It retains the
upstream GPL licence, attribution, artwork and translation files. See
[LICENSE](LICENSE) and [Translations](TRANSLATIONS.md).

Report fork issues at [EthanSK/obs-aitum-stream-suite](https://github.com/EthanSK/obs-aitum-stream-suite/issues).
For upstream software and documentation, see [Aitum Stream Suite](https://github.com/Aitum/obs-aitum-stream-suite)
and [OBS Studio](https://obsproject.com/). Follow each upstream project's
contribution policy before submitting changes there.
