# OpenHD-Glide WSL Manual

These commands target `Ubuntu-22.04` on WSL. The current WSL install puts the binaries in `/usr/local/bin`:

```sh
openhd-glide
glide-flow
glide-ui
glide-view
glide-send
```

From PowerShell, prefix commands with:

```powershell
wsl -d Ubuntu-22.04 --
```

For example:

```powershell
wsl -d Ubuntu-22.04 -- glide-ui --preview --width 760 --height 720 --x 80 --y 40
```

## Independent UI Preview

Run the LVGL sidebar UI by itself:

```sh
glide-ui --preview --width 760 --height 720 --x 80 --y 40
```

PowerShell form:

```powershell
wsl -d Ubuntu-22.04 -- glide-ui --preview --width 760 --height 720 --x 80 --y 40
```

## Independent Flow Preview

Run the OpenGL ES Flow OSD preview by itself:

```sh
glide-flow --preview --width 1280 --height 720 --x 80 --y 40
```

PowerShell form:

```powershell
wsl -d Ubuntu-22.04 -- glide-flow --preview --width 1280 --height 720 --x 80 --y 40
```

## Independent Video Decode Worker

Run `glide-view` as a decode-only UDP worker:

```sh
glide-view --udp-video --udp-port 5600 --udp-codec h264
```

PowerShell form:

```powershell
wsl -d Ubuntu-22.04 -- glide-view --udp-video --udp-port 5600 --udp-codec h264
```

Feed it from another Ubuntu/WSL terminal with a test stream:

```sh
/usr/local/share/openhd-glide/examples/stream-videotestsrc-to-glide-view.sh 127.0.0.1 5600
```

For the 1080p120 Blurbusters file stream:

```sh
/mnt/c/Users/Raphael/openhd-glide/examples/stream-blurbusters-1080p120-to-glide-view.sh 127.0.0.1 5600 h264
```

## Full WSL Preview Stack

Run the controller-owned WSL preview stack:

```sh
openhd-glide --preview-stack --preview-width 1280 --flow-height 720 --ui-width 760 --preview-x 60 --preview-y 40 --ui-opacity 1.0
```

PowerShell form:

```powershell
wsl -d Ubuntu-22.04 -- openhd-glide --preview-stack --preview-width 1280 --flow-height 720 --ui-width 760 --preview-x 60 --preview-y 40 --ui-opacity 1.0
```

The UI `OSD` panel has switches for the video FPS overlay, coordinates, and the speed/altitude ladder versus compact text mode. The wind indicator is drawn by Flow in the performance horizon, so run the controller-owned preview stack or `examples/run-wsl-ui-preview.sh` without `GLIDE_UI_ONLY=1` when checking it.

## Rebuild And Reinstall

Use this when source changes should be installed into `/usr/local/bin`:

```sh
cd /mnt/c/Users/Raphael/openhd-glide
cmake -S . -B "$HOME/openhd-glide-build-wsl" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DOPENHD_GLIDE_DEVICE_KMS=OFF
cmake --build "$HOME/openhd-glide-build-wsl" --target openhd-glide glide-flow glide-ui glide-view glide-send -j"$(nproc)"
sudo cmake --install "$HOME/openhd-glide-build-wsl" --prefix /usr/local
```

If `sudo` asks for a password and you are launching from PowerShell, install as root with:

```powershell
wsl -d Ubuntu-22.04 -u root -- cmake --install /home/damien/openhd-glide-build-wsl --prefix /usr/local
```

## Installed Dependencies

The Ubuntu WSL environment has the development/runtime packages needed for the previews and UDP decode, including SDL2, GLES/EGL, FreeType, and GStreamer plugins.
