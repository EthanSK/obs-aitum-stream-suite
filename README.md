# Aitum++ (fork of Aitum Stream Suite)

Additive fork of Aitum Stream Suite 1.2.1. Advanced Workspaces opens by default after preserving the normal OBS dock layout; the Tools toggles switch between the workspace UI and the familiar OBS view without changing the theme:

- `Tools -> Aitum++ Controls` toggles the minimum useful controls (extra canvas docks + outputs dock). Unchecking restores the familiar OBS view.
- `Tools -> Aitum++ Workspaces (Advanced)` toggles the full workspace toolbar (Live/Build/custom dock layouts) and is checked by default on each launch.
- `Tools -> Aitum++ Settings` opens the canvas/output configuration dialog (also reachable from the Outputs dock gear).

The fork removes the upstream first-run appearance changes (theme switch, vertical mixer, hidden context toolbars) and no longer converts the main OBS preview into a dock unless `Aitum/MainCanvasDock=true` is set in the OBS user config. Existing `aitum.json` profile configuration (canvases, outputs, dock layouts) loads unchanged, and nothing is ever streamed, recorded or started automatically.

Plugin for [OBS Studio](https://github.com/obsproject/obs-studio) to add [![Aitum logo](media/aitum.png) Aitum](https://aitum.tv)

# Build
- In-tree build
    - Build OBS Studio: https://obsproject.com/wiki/Install-Instructions
    - Check out this repository to UI/frontend-plugins/aitum-stream-suite
    - Add `add_subdirectory(aitum-stream-suite)` to UI/frontend-plugins/CMakeLists.txt
    - Rebuild OBS Studio
- Stand-alone build
    - Verify that you have development files for OBS
    - Check out this repository and run `cmake -S . -B build -DBUILD_OUT_OF_TREE=On && cmake --build build`

# Translations
Please read [Translations](TRANSLATIONS.md)
