@REM ################################################################################
@REM OpenHD
@REM
@REM Licensed under the GNU General Public License (GPL) Version 3.
@REM
@REM This software is provided "as-is," without warranty of any kind, express or
@REM implied, including but not limited to the warranties of merchantability,
@REM fitness for a particular purpose, and non-infringement. For details, see the
@REM full license in the LICENSE file provided with this source code.
@REM
@REM Non-Military Use Only:
@REM This software and its associated components are explicitly intended for
@REM civilian and non-military purposes. Use in any military or defense
@REM applications is strictly prohibited unless explicitly and individually
@REM licensed otherwise by the OpenHD Team.
@REM
@REM Contributors:
@REM A full list of contributors can be found at the OpenHD GitHub repository:
@REM https://github.com/OpenHD
@REM
@REM © OpenHD, All Rights Reserved.
@REM ################################################################################

@echo off
setlocal

set TARGET=%1
if "%TARGET%"=="" (
  echo usage: %0 ^<target-ip^> [port] 1>&2
  echo example local test: %0 127.0.0.1 5600 1>&2
  exit /b 2
)

set PORT=%2
if "%PORT%"=="" set PORT=5600

set ENCODER_NAME=
set ENCODER=

gst-inspect-1.0 nvh264enc >nul 2>nul
if %ERRORLEVEL%==0 (
  set ENCODER_NAME=nvh264enc hardware encoder
  set ENCODER=nvh264enc bitrate=4000 gop-size=30 bframes=0 zerolatency=true
  goto encoder_ready
)

gst-inspect-1.0 qsvh264enc >nul 2>nul
if %ERRORLEVEL%==0 (
  set ENCODER_NAME=qsvh264enc hardware encoder
  set ENCODER=qsvh264enc bitrate=4000 gop-size=30
  goto encoder_ready
)

gst-inspect-1.0 d3d11h264enc >nul 2>nul
if %ERRORLEVEL%==0 (
  set ENCODER_NAME=d3d11h264enc hardware encoder
  set ENCODER=d3d11h264enc bitrate=4000
  goto encoder_ready
)

gst-inspect-1.0 amfh264enc >nul 2>nul
if %ERRORLEVEL%==0 (
  set ENCODER_NAME=amfh264enc hardware encoder
  set ENCODER=amfh264enc bitrate=4000
)

:encoder_ready
if "%ENCODER%"=="" (
  if "%GLIDE_ALLOW_SOFTWARE_ENCODER%"=="1" (
    set ENCODER_NAME=x264enc software fallback
    set ENCODER=x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 bframes=0 byte-stream=true aud=true
  ) else (
    echo No hardware H.264 encoder found. Install/enable nvh264enc, qsvh264enc, d3d11h264enc, or amfh264enc. 1>&2
    echo Set GLIDE_ALLOW_SOFTWARE_ENCODER=1 only for non-performance fallback testing. 1>&2
    exit /b 1
  )
)

echo Streaming RTP/H264 test video to %TARGET%:%PORT% using %ENCODER_NAME%

gst-launch-1.0 -v ^
  videotestsrc is-live=true pattern=smpte ! ^
  videoconvert ! ^
  video/x-raw,format=I420,width=1280,height=720,framerate=60/1 ! ^
  %ENCODER% ! ^
  h264parse config-interval=1 ! ^
  video/x-h264,stream-format=byte-stream,alignment=au ! ^
  rtph264pay pt=96 config-interval=1 ! ^
  udpsink host=%TARGET% port=%PORT% sync=false async=false
