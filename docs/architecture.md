# OpenHD-Glide Architecture

OpenHD-Glide is structured as a controller plus three workload binaries. The controller is responsible for system discovery, process startup, lifecycle supervision, CPU affinity assignment, and the initial display-plane allocation strategy.

## Processes

- `openhd-glide`: controller process. Performs DRM/KMS and CPU discovery first, then starts and initializes the workload binaries.
- `glide-view`: video plane worker. Receives video over UDP and renders it to the assigned display target.
- `glide-flow`: OSD worker. Renders telemetry and overlay content with OpenGL ES.
- `glide-ui`: UI worker. Hosts the interactive UI layer with LVGL; the current development backend is LVGL's SDL window driver.

The current repository builds all four executables. The intended deployed system still has three rendering/UI workload binaries; `openhd-glide` is the supervising controller.

## Startup Sequence

1. Controller starts.
2. Controller probes DRM/KMS devices and reports all visible planes, positions, sizes, supported formats, and possible CRTCs.
3. Controller probes CPU topology and reports logical cores, online state, package/core IDs, and current/max frequencies where the platform exposes them.
4. Controller selects core assignments for `glide-view`, `glide-flow`, and `glide-ui`.
5. Controller starts the three workers and passes their initial configuration.
6. Workers report readiness.
7. Controller begins health monitoring and restart policy handling.

## Display Model

The first hardware probe uses DRM/KMS planes because the target pipeline needs deterministic composition:

- video can occupy a dedicated plane where supported;
- OSD can use an overlay plane or an OpenGL ES surface;
- UI can use a separate plane or be composited above OSD, depending on hardware limits.

The probe currently reports all planes exposed through `libdrm` universal planes. Plane claiming and atomic modesetting are the next step.

## GlideView Video

`glide-view --udp-video` receives RTP/H.264 on UDP port 5600 by default and renders with GStreamer's `kmssink`. The intended device pipeline is:

- `udpsrc`
- `rtph264depay`
- `h264parse`
- hardware-oriented `v4l2h264dec` or `v4l2slh264dec`
- `kmssink`

The queues are constrained to one buffer and leaky mode to keep latency low. `glide-view` accepts `--plane-id` and `--connector-id` and forwards those to `kmssink` when available. The controller exposes these as `--view-plane-id`, `--view-connector-id`, and `--view-udp-port`.

The current composition limitation is that `glide-flow --kms` still owns fullscreen scanout. For real video-under-OSD composition, Flow needs to move to an alpha-capable overlay plane above the View plane.

## GlideFlow OSD

The first OSD elements are an FPS counter positioned in the bottom-left corner, a compact performance horizon centered on the render surface, left/right/bottom link overview panels, a left-side speed widget, and a right-side altitude widget. The current implementation includes:

- frame-rate measurement over a one-second window;
- deterministic top-right text layout with a fixed bitmap font;
- an OpenGL ES 2.0 text renderer that prefers FreeType antialiased TrueType glyphs and falls back to lightweight vector strokes when FreeType is unavailable.
- a port of QOpenHD's `PerformanceHorizonWidget2`: split horizon bars shifted by pitch, rotated by roll, with a fixed center circle/crossbar.
- a port of QOpenHD's left and right link overview widgets: downlink RSSI/temp/MCS/SNR blocks and uplink/frequency/bitrate/record/RC blocks.
- a port of QOpenHD's bottom link overview widget: full-width bottom panel with center flight mode/time notch and telemetry slots.
- a port of QOpenHD's speed widget: left-side pointer box plus vertical ladder ticks/labels.
- a port of QOpenHD's altitude widget: right-side pointer box plus vertical ladder ticks/labels.
- simulated attitude data for preview until telemetry IPC exists.
- simulated link data for preview until telemetry IPC exists.
- simulated speed data for preview until telemetry IPC exists.
- simulated altitude data for preview until telemetry IPC exists.

The next rendering step is to add the DRM/EGL surface setup so `glide-flow` can own a real overlay plane or render target instead of only exercising the layout path from the command-line binary.

## Development Preview

`glide-flow` uses SDL2/OpenGL ES preview and `glide-ui` uses LVGL's SDL preview backend for development on WSL or desktop systems without DRM/KMS. These backends are intentionally convenience layers. They are slower and less deterministic than the target DRM/KMS layout, but they allow OSD and UI work to be developed visually before the production display path is available.

Preview commands:

- `glide-flow --preview --width 1280 --height 720`
- `glide-ui --preview --width 1280 --height 220`
- `openhd-glide --preview-stack`

`openhd-glide --preview-stack` is the current WSL controller path. It performs the same discovery step,
then starts `glide-flow` and `glide-ui` as child processes with explicit SDL window positions. The default
layout places a narrow UI sidebar window over the left edge of the Flow window, using borderless windows.
This avoids the unreliable transparent-window behavior in WSL while still exercising UI-to-Flow control.
Use `--preview-width`, `--flow-height`, `--ui-width`, `--ui-height`, `--preview-x`, `--preview-y`, and
`--ui-opacity` to tune the layout. `--vertical-stack` keeps the older UI-above-Flow layout for comparison.

The WSL preview stack starts a controller-owned Unix domain socket at `/tmp/openhd-glide.sock` by default.
`glide-view`, `glide-flow`, and `glide-ui` register with `hello <worker>` messages and can exchange simple
line-based status/control messages with the controller. `glide-ui` currently uses this path for an FPS toggle
inside the preview sidebar `MISC` panel; the controller receives `set fps 0/1` and broadcasts `state fps 0/1`, which `glide-flow`
uses before drawing the FPS overlay. The socket path can be changed with `--ipc-socket`.

The LVGL UI is intentionally backend-isolated: the current SDL display driver is for WSL/Windows-style iteration. The target backend should render the same LVGL tree into shared buffers or a plane-owned buffer path supplied by `openhd-glide`, without letting `glide-ui` become DRM/KMS master.

## Device KMS Mode

`openhd-glide --kms-stack` is the target-device entry point. It starts the same Unix-socket controller and launches:

- `glide-view --stay-alive`
- `glide-flow --kms --stay-alive`
- `glide-ui --headless`

This mode is intentionally separate from the SDL preview path. It validates device DRM/KMS plane discovery, process startup, CPU assignment, and IPC without creating SDL windows. `glide-flow --kms` creates a fullscreen KMS/GBM/EGL scanout surface on the active connector and renders the current OSD through OpenGL ES. UI remains headless in this path until LVGL can render into a shared-buffer or plane-backed target owned by the controller.

## CPU Model

Each worker is expected to run on a different CPU core where the target hardware makes that useful. The controller uses discovered topology and frequency data to prefer faster online cores for video and rendering workloads.

Priority order is:

1. `glide-view`
2. `glide-flow`
3. `glide-ui`

`glide-view` always gets its own core and prefers the fastest online core. `glide-flow` gets the next fastest core when possible. `glide-ui` is lowest priority, but still gets the next fastest isolated core when enough cores are available. If only two usable cores are available, `glide-ui` shares with `glide-flow` so the video renderer remains isolated.

If an existing OpenHD process is detected, the controller avoids `cpu0` for worker assignment so the base OpenHD process can keep that core.

The assignment policy is implemented, but process launch and actual OS affinity application are still next.

## IPC Direction

The controller-to-worker API starts as a small Unix socket protocol and is not locked yet. The planned boundary is:

- startup configuration: plane IDs, UDP ports, render dimensions, CPU affinity, and feature flags;
- runtime commands: start, stop, pause, reconfigure, set overlay state, set UI state;
- health: ready, heartbeat, frame/render stats, and fatal errors.

Unix domain sockets are the default Linux target because they are simple, local, and easy to supervise.
The current prototype is newline-delimited text:

- `hello <worker>`
- `status <worker> <details>`
- `heartbeat <worker>`
- `get fps`
- `set fps 0|1`
- `state fps 0|1`
