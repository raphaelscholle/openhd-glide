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

Or use the device helper script, which configures, builds, and installs the binaries:

```sh
scripts/compile-install.sh
```

To install the common build/runtime dependencies first:

```sh
scripts/compile-install.sh --deps
```

The helper accepts environment overrides:

```sh
GLIDE_BUILD_DIR=build-kms GLIDE_INSTALL_PREFIX=/usr/local GLIDE_JOBS=8 scripts/compile-install.sh
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
sudo ./build-kms/openhd-glide --kms-stack --preview-width 1920 --flow-height 1080 --display-refresh-hz 120
```

`--kmd-stack` is accepted as an alias for `--kms-stack`. The current device path starts the three workers and IPC,
uses DRM/KMS discovery from the controller, runs `glide-flow` through GBM/EGL directly on the active KMS connector,
and runs `glide-ui` headless until the LVGL shared-buffer/plane backend exists.

`glide-view` listens for RTP/H.264 on UDP port 5600 by default and decodes through GStreamer into `appsink`.
It intentionally does not use `kmssink`, because `kmssink` would compete for DRM/KMS master. The next production step is
Unix-socket FD passing so `glide-view` can hand decoded DMABUFs to `openhd-glide`, while only `openhd-glide` imports
buffers and programs KMS planes.

```sh
sudo ./build-kms/openhd-glide --kms-stack --view-udp-port 5600 --preview-width 1920 --flow-height 1080 --display-refresh-hz 120
```

Standalone View decode test:

```sh
sudo ./build-kms/glide-view --udp-video --udp-port 5600
```

This command does not display video. It should log `first decoded sample ...` and `decoded fps=...` once RTP/H.264
frames arrive. If those lines do not appear, the sender is not reaching the receiver or the stream caps do not match.
On Allwinner BSP images, the hardware path may use `omxh264dec` instead of `v4l2h264dec`, and running under `sudo` is
often required because the cedar and DMA heap device nodes are root-only by default.

Temporary controller-owned KMS video preview:

```sh
sudo ./build-kms/openhd-glide --kms-video-preview --view-udp-port 5600 --preview-width 1920 --flow-height 1080
```

This displays the UDP video without `kmssink` by decoding in `openhd-glide`, requesting DMABUF output from the hardware
decoder, importing the decoded FD into DRM, and scanning it out on a KMS video plane. It still uses a black primary
framebuffer only to keep the CRTC active; Flow and UI should become separate overlay planes above this video plane.

Example run scripts cover the current device modes. Each script takes the UDP video port as its first optional
argument, defaulting to `5600`; set `GLIDE_WIDTH` and `GLIDE_HEIGHT` to override the default `1920x1080`.
Device KMS scripts default to `GLIDE_DISPLAY_HZ=120`; override it if the panel should use a different mode.

```sh
# GStreamer/OMX decode, KMS video plane plus Flow overlay at full video rate.
examples/run-kms-video-gstreamer-flow.sh 5600

# GStreamer/OMX decode, KMS video plane plus Flow overlay capped to 30 fps.
examples/run-kms-video-gstreamer-flow-30fps.sh 5600

# GStreamer/OMX decode, fastest video-only legacy KMS plane path.
examples/run-kms-video-gstreamer-video-only.sh 5600

# Native Cedar RTP/H.264 decode, KMS video plane plus Flow overlay.
examples/run-kms-video-cedar-flow.sh 5600

# Native Cedar RTP/H.264 decode, fastest video-only legacy KMS plane path.
examples/run-kms-video-cedar-video-only.sh 5600

# Standalone glide-view decode-only test.
examples/run-glide-view-decode-only.sh 5600

# Multi-process KMS stack smoke test.
examples/run-kms-stack.sh 5600
```

Installed helper scripts are placed in `${CMAKE_INSTALL_PREFIX}/share/openhd-glide/examples`.

Send a test pattern from another machine with GStreamer:

```sh
examples/stream-videotestsrc-to-glide-view.sh <target-ip> 5600
```

Use the Pi's actual network IP as `<target-ip>`. For a sender running on the same Pi, use `127.0.0.1`.
The Linux sender requires a hardware encoder such as `v4l2h264enc` by default. Set
`GLIDE_ALLOW_SOFTWARE_ENCODER=1` only when you intentionally want a non-performance `x264enc` fallback.
The script performs a small encoder self-test first. If `v4l2h264enc` fails with `bcm2835-codec ... ret -3` in
`dmesg`, the Raspberry Pi encoder driver is failing independently of Glide; use another sender with hardware H.264 or
fix the Pi encoder stack before using it for performance measurements.

To avoid sender-side encoding entirely, stream a downloaded H.264 MP4:

```sh
examples/stream-h264-file-to-glide-view.sh <target-ip> 5600
```

This downloads a pre-encoded Big Buck Bunny H.264 1080p60 30-second MP4 by default, then streams only
`filesrc ! qtdemux ! h264parse ! rtph264pay ! udpsink`. There is no `videotestsrc` and no encoder. To use a 720p60
H.264 MP4 instead, pass the local file path as the third argument or set `GLIDE_TEST_VIDEO_URL` before running the
script.

On Windows, use:

```bat
examples\stream-videotestsrc-to-glide-view.bat <target-ip> 5600
```

The Windows sender requires a hardware encoder (`nvh264enc`, `qsvh264enc`, `d3d11h264enc`, `amfh264enc`) by default.
Set `GLIDE_ALLOW_SOFTWARE_ENCODER=1` only when you intentionally want a non-performance `x264enc` fallback.

Windows can also stream the downloaded H.264 file without encoding:

```bat
examples\stream-h264-file-to-glide-view.bat <target-ip> 5600
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
