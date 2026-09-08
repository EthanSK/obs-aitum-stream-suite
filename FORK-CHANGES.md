# Changes from Aitum Stream Suite 1.2.1

The fork starts at upstream `f0b3419` (tag `1.2.1`). The history preserves each
change rather than replacing the upstream project with a source snapshot.

| Commit | Changes |
| --- | --- |
| [ac1963a](https://github.com/EthanSK/obs-aitum-stream-suite/commit/ac1963a) | Adds grouped multistream controls, bitrate/drop/congestion diagnostics, Apple VideoToolbox dimension alignment, and the reversible Controls/Workspaces UI. Keeps upstream theme and ordinary OBS layout choices. Removes obsolete AGL linkage from Qt's OpenGL wrapper on current Xcode. |
| [fb15546](https://github.com/EthanSK/obs-aitum-stream-suite/commit/fb15546) | Makes the canvas preview boundary visible. |
| [4576cfc](https://github.com/EthanSK/obs-aitum-stream-suite/commit/4576cfc) | Retains an active/reconnecting output through deactivation so a retry thread cannot use freed output state. |
| [5e8c7c8](https://github.com/EthanSK/obs-aitum-stream-suite/commit/5e8c7c8) | Adds macOS screen-capture restart and the OBScene restart handoff. |
| [01c3d52](https://github.com/EthanSK/obs-aitum-stream-suite/commit/01c3d52) | Adds administrator-authorized camera-service restart and reopens OBS camera sources. |
| [1dd3522](https://github.com/EthanSK/obs-aitum-stream-suite/commit/1dd3522), [6ad0ebe](https://github.com/EthanSK/obs-aitum-stream-suite/commit/6ad0ebe) | Keeps recovery controls compact and visually distinct. |
| [fee31bf](https://github.com/EthanSK/obs-aitum-stream-suite/commit/fee31bf) | Allows camera recovery while outputs remain running; brief camera-frame loss remains possible. |

## Limits that matter

- Restart Screen Capture only uses an enabled native OBS restart action. It does not force recovery by repeatedly changing source settings.
- Restart Camera Services affects macOS camera services, including cameras used by other apps, and requires administrator approval when invoked.
- Restart OBS needs the separate OBScene app. It does not embed OBScene or replace its safety checks.
- Apple H.264 dimension alignment changes the encoder output size slightly. It is not a change to the source canvas or the separate main OBS recording.
- Health numbers come from OBS's actual output counters; CPU belongs to the shared OBS process. A zero-drop reading is not proof that every captured frame is visually correct.

Verified against source on 8 September 2026. The installed OBS log identifies
the Aitum++ 1.2.1 fork, but its version alone does not identify an exact Git
revision; no new native recovery test or clean build was run for this docs update.
