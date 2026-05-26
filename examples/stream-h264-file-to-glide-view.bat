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
  echo usage: %0 ^<target-ip^> [port] [h264-mp4-file] 1>&2
  echo example: %0 127.0.0.1 5600 1>&2
  exit /b 2
)

set PORT=%2
if "%PORT%"=="" set PORT=5600

set VIDEO_FILE=%3
if "%VIDEO_FILE%"=="" set VIDEO_FILE=examples\media\big-buck-bunny-1080p-60fps-30sec.mp4

set MODE=%4
set SINK_SYNC=true
set MODE_LABEL=looping, realtime/timestamp-paced send
if "%MODE%"=="--fast" set SINK_SYNC=false
if "%MODE%"=="fast" set SINK_SYNC=false
if "%SINK_SYNC%"=="false" set MODE_LABEL=looping, unpaced full-speed send

set VIDEO_URL=%GLIDE_TEST_VIDEO_URL%
if "%VIDEO_URL%"=="" set VIDEO_URL=https://github.com/chthomos/video-media-samples/raw/refs/heads/master/big-buck-bunny-1080p-60fps-30sec.mp4

if not exist "%VIDEO_FILE%" (
  for %%I in ("%VIDEO_FILE%") do if not exist "%%~dpI" mkdir "%%~dpI"
  echo Downloading pre-encoded H.264 test video:
  echo   %VIDEO_URL%
  echo   -^> %VIDEO_FILE%
  curl.exe -L --fail --continue-at - --output "%VIDEO_FILE%" "%VIDEO_URL%"
  if errorlevel 1 exit /b 1
)

echo Streaming pre-encoded H.264 file to %TARGET%:%PORT%
echo   file=%VIDEO_FILE%
echo   pipeline=filesrc ! qtdemux ! h264parse ! rtph264pay ! udpsink
echo   no videotestsrc, no encoder
echo   mode=%MODE_LABEL%

:loop
gst-launch-1.0 -q ^
  filesrc location="%VIDEO_FILE%" ! ^
  qtdemux name=demux ^
  demux.video_0 ! ^
  queue max-size-buffers=0 max-size-bytes=0 max-size-time=0 ! ^
  h264parse config-interval=1 ! ^
  video/x-h264,stream-format=byte-stream,alignment=au ! ^
  rtph264pay pt=96 config-interval=1 mtu=1200 ! ^
  udpsink host=%TARGET% port=%PORT% sync=%SINK_SYNC% async=false
if errorlevel 1 exit /b 1
goto loop
