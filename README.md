# openhd-glide

OpenHD-Glide is the controller for a three-worker rendering stack:

- `glide-view`: UDP video rendering worker
- `glide-flow`: OpenGL ES OSD worker
- `glide-ui`: LVGL-based UI worker

The first implemented controller step probes DRM/KMS planes and CPU topology.
It also computes the first worker CPU assignment plan: `glide-view` gets the strongest isolated core, `glide-flow` gets the next priority, and `glide-ui` is lowest priority.

## Build

```sh
cmake -S . -B build
cmake --build build
```

For a target device DRM/KMS build:

```sh
sudo apt update
sudo apt install -y build-essential cmake pkg-config libdrm-dev libgbm-dev libgles2-mesa-dev libegl1-mesa-dev libfreetype-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-libav
cmake --preset device-kms
cmake --build --preset device-kms -j$(nproc)
```

If the device CMake is too old for presets, use the equivalent direct configure:

```sh
cmake -S . -B build-kms -DCMAKE_BUILD_TYPE=Release -DOPENHD_GLIDE_DEVICE_KMS=ON
cmake --build build-kms -j$(nproc)
```

On Linux, install `libdrm` development headers to enable real DRM plane discovery. Without `libdrm`, or on non-Linux platforms, the probe builds and reports that DRM discovery is unavailable.

Install OpenGL ES 2.0 development files to enable the first `glide-flow` renderer path. Until the DRM/EGL surface is added, `glide-flow` runs the FPS layout path and prints where the top-right counter will render. Passing `--render-gles` submits the FPS glyphs through GLES and requires a current EGL/GLES context.
Install SDL2 development files to enable desktop preview windows for WSL/Windows development. The preview path is intentionally not the production display path; it exists so `glide-flow` and `glide-ui` can be developed without DRM/KMS.
Install FreeType development files to enable antialiased TrueType OSD text. Without FreeType, `glide-flow` falls back to the simpler stroke font.
`glide-ui` fetches LVGL v9.2.2 at configure time and uses LVGL's SDL backend for the development preview.

## Run

```sh
./build/openhd-glide
```

Target device KMS stack test:

```sh
sudo ./build-kms/openhd-glide --kms-stack --preview-width 1920 --flow-height 1080
```

`--kmd-stack` is accepted as an alias for `--kms-stack`. The current device path starts the three workers and IPC,
uses DRM/KMS discovery from the controller, runs `glide-flow` through GBM/EGL directly on the active KMS connector,
and runs `glide-ui` headless until the LVGL shared-buffer/plane backend exists.

`glide-view` listens for RTP/H.264 on UDP port 5600 by default and decodes through GStreamer into `appsink`.
It intentionally does not use `kmssink`, because `kmssink` would compete for DRM/KMS master. The next production step is
Unix-socket FD passing so `glide-view` can hand decoded DMABUFs to `openhd-glide`, while only `openhd-glide` imports
buffers and programs KMS planes.

```sh
sudo ./build-kms/openhd-glide --kms-stack --view-udp-port 5600 --preview-width 1920 --flow-height 1080
```

Standalone View decode test:

```sh
sudo glide-view --udp-video --udp-port 5600
```

This command does not display video. It should log `first decoded sample ...` and `decoded fps=...` once RTP/H.264
frames arrive. If those lines do not appear, the sender is not reaching the receiver or the stream caps do not match.

Temporary controller-owned KMS video preview:

```sh
sudo openhd-glide --kms-video-preview --view-udp-port 5600 --preview-width 1920 --flow-height 1080
```

This displays the UDP video without `kmssink` by decoding in `openhd-glide`, requesting DMABUF output from the hardware
decoder, importing the decoded FD into DRM, and scanning it out on a KMS video plane. It still uses a black primary
framebuffer only to keep the CRTC active; Flow and UI should become separate overlay planes above this video plane.

Send a test pattern from another machine with GStreamer:

```sh
examples/stream-videotestsrc-to-glide-view.sh <target-ip> 5600
```

Use the Pi's actual network IP as `<target-ip>`. For a sender running on the same Pi, use `127.0.0.1`.

On Windows, use:

```bat
examples\stream-videotestsrc-to-glide-view.bat <target-ip> 5600
```

```sh
./build/glide-flow --width 1920 --height 1080
```

```sh
./build/glide-flow --preview --width 1280 --height 720
./build/glide-ui --preview --width 1280 --height 220
./build/openhd-glide --preview-stack
```

For WSL development, `openhd-glide --preview-stack` starts `glide-flow` first and then places a
`glide-ui` LVGL/SDL preview over the left side. In WSL, the UI preview is kept as a sidebar surface to avoid
covering the Flow preview. The sidebar contains a `MISC` panel with an FPS overlay toggle wired through the
controller Unix socket.
The layout can be adjusted:

```sh
./build/openhd-glide --preview-stack --preview-width 1280 --flow-height 720 --ui-width 760 --preview-x 60 --preview-y 40 --ui-opacity 1.0
```

The default development IPC socket is `/tmp/openhd-glide.sock`; override it with `--ipc-socket <path>`.

See [docs/architecture.md](docs/architecture.md) for the initial process architecture.
See [AGENTS.md](AGENTS.md) for project goals and current implementation notes for future AI agents.
