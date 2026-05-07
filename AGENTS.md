# OpenHD-Glide Agent Notes

## Goal

OpenHD-Glide is a lightweight multi-process rendering stack for OpenHD. The goal is smooth, low-overhead display composition using DRM/KMS layout and OpenGL ES while keeping video completely decoupled from OSD and UI rendering.

The video path has priority over everything else. `GlideView` should be able to render incoming UDP video without being blocked by OSD or UI work. `GlideFlow` renders lightweight vector-style OSD graphics with OpenGL ES. `GlideUI` provides the user interface layer, likely using LVGL.

## Process Model

- `openhd-glide`: controller. Probes hardware, assigns CPU cores, starts workers, and will supervise lifecycle/IPC.
- `glide-view`: highest-priority video renderer. It should always have its own CPU core and should prefer a big/fast core.
- `glide-flow`: second-priority OSD renderer. It should prefer a big/fast core after `glide-view`; it may use a small core if required.
- `glide-ui`: lowest-priority UI worker. It uses LVGL for the UI layer. It can run on a small core if needed, but should still get a fast isolated core when enough cores are available.

## Current State

- CMake project scaffold exists.
- `openhd-glide` probes DRM/KMS planes through `libdrm` when available.
- `openhd-glide` probes CPU topology and frequency data from Linux sysfs when available.
- `glide-flow` has the first OSD element: an FPS counter laid out in the top-right corner.
- `glide-flow` includes a FreeType-backed antialiased text path for polished OSD text, with the old vector stroke font retained only as fallback.
- `glide-flow` ports QOpenHD's `PerformanceHorizonWidget2` as a GLES performance horizon with simulated roll/pitch data for now.
- `glide-flow` ports QOpenHD's left/right link overview widgets as GLES panels with simulated link data for now.
- `glide-flow` ports QOpenHD's bottom link overview bar as a GLES full-width bottom panel with simulated slot data for now.
- `glide-flow` ports QOpenHD's altitude widget as a GLES right-side pointer and ladder with simulated altitude data for now.
- `glide-flow` ports QOpenHD's speed widget as a GLES left-side pointer and ladder with simulated speed data for now.
- A development preview backend exists using SDL2 plus OpenGL ES. It is for WSL/Windows-style development only and does not replace the production DRM/KMS path.
- `glide-ui` now uses LVGL v9.2.2 with LVGL's SDL backend for WSL development. Do not revive the hand-drawn GLES UI path.
- `openhd-glide --preview-stack` runs the WSL development stack by launching `glide-flow` first and an LVGL/SDL `glide-ui` sidebar preview over the left side.
- `openhd-glide --kms-stack` is the target-device stack entry point. It currently validates DRM/KMS probing, process startup, CPU assignment, and Unix-socket IPC. `--kmd-stack` is accepted as a typo-compatible alias.
- The WSL preview stack starts a controller-owned Unix socket at `/tmp/openhd-glide.sock` by default. Workers register with `hello <worker>`. `glide-ui` toggles the Flow FPS overlay by sending `set fps 0/1`; the controller broadcasts `state fps 0/1` to `glide-flow`.
- `glide-ui` has a first LVGL QOpenHD-style sidebar shell: large icon rail, `Find Air Unit` scan panel, and an FPS overlay toggle in the `MISC` panel. In WSL the UI window is sized as a sidebar surface, not a transparent full-screen overlay.
- On the device stack, `glide-ui --headless` is used until the LVGL shared-buffer/plane backend exists. Do not make the UI a DRM/KMS master.

## CPU Assignment Policy

Core priority is:

1. `glide-view`
2. `glide-flow`
3. `glide-ui`

Assignment rules:

- `glide-view` always gets its own core.
- Prefer the fastest online core for `glide-view`.
- Prefer the next fastest online core for `glide-flow`.
- Prefer the next fastest remaining online core for `glide-ui` when enough cores are available.
- If there are not enough cores for all workers, keep `glide-view` isolated and let lower-priority workers share a non-view core.
- If an existing OpenHD process is running, avoid assigning workers to `cpu0`.
- Single-core systems are not a target for this project.

## Next Work

- Add DRM/EGL surface ownership for `glide-flow`.
- Replace the temporary `glide-flow --kms` IPC/timing loop with the real DRM/EGL plane surface renderer.
- Grow the SDL2 preview backend only where it helps developer iteration; keep production assumptions aligned with DRM/KMS.
- Add process launching and CPU affinity application in `openhd-glide`.
- Add worker IPC and readiness/heartbeat reporting.
- Add `glide-view` UDP video ingest and DRM plane rendering.
- Add LVGL-backed `glide-ui`.
