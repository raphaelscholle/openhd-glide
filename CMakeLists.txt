cmake_minimum_required(VERSION 3.20)

project(openhd-glide
    VERSION 0.1.0
    LANGUAGES CXX
)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX AND NOT APPLE)
    set(FETCHCONTENT_BASE_DIR "/tmp/openhd-glide-fetchcontent" CACHE PATH "FetchContent cache outside drvfs" FORCE)
endif()

option(OPENHD_GLIDE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(OPENHD_GLIDE_DEVICE_KMS "Enable the device-oriented DRM/KMS runtime mode" ON)

add_library(glide_common
    src/common/ipc.cpp
    src/common/logging.cpp
    src/common/preview_control.cpp
    src/platform/core_assignment.cpp
    src/platform/cpu_topology.cpp
    src/platform/drm_probe.cpp
    src/platform/process_probe.cpp
)

target_include_directories(glide_common
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(glide_common PRIVATE /W4)
    if(OPENHD_GLIDE_WARNINGS_AS_ERRORS)
        target_compile_options(glide_common PRIVATE /WX)
    endif()
else()
    target_compile_options(glide_common PRIVATE -Wall -Wextra -Wpedantic)
    if(OPENHD_GLIDE_WARNINGS_AS_ERRORS)
        target_compile_options(glide_common PRIVATE -Werror)
    endif()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBDRM QUIET libdrm)
    pkg_check_modules(GBM QUIET gbm)
    pkg_check_modules(EGL QUIET egl)
    pkg_check_modules(GLESV2 QUIET glesv2)
    pkg_check_modules(SDL2 QUIET sdl2)
    pkg_check_modules(FREETYPE QUIET freetype2)
    pkg_check_modules(GSTREAMER QUIET gstreamer-1.0 gstreamer-app-1.0 gstreamer-allocators-1.0 gstreamer-video-1.0)
endif()

if(SDL2_FOUND)
    set(LV_CONF_PATH ${CMAKE_CURRENT_SOURCE_DIR}/src/glide_ui/lv_conf.h CACHE STRING "" FORCE)
    set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
    set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
    set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v9.2.2
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(lvgl)
    target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(lvgl PRIVATE ${SDL2_LIBRARIES})
    target_link_directories(lvgl PRIVATE ${SDL2_LIBRARY_DIRS})
endif()

if(LIBDRM_FOUND)
    target_compile_definitions(glide_common PRIVATE OPENHD_GLIDE_HAS_LIBDRM=1)
    target_include_directories(glide_common PRIVATE ${LIBDRM_INCLUDE_DIRS})
    target_link_libraries(glide_common PRIVATE ${LIBDRM_LIBRARIES})
    target_link_directories(glide_common PRIVATE ${LIBDRM_LIBRARY_DIRS})
else()
    target_compile_definitions(glide_common PRIVATE OPENHD_GLIDE_HAS_LIBDRM=0)
endif()

function(add_glide_executable target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} PRIVATE glide_common)
endfunction()

add_library(glide_dev
    src/dev/kms_dmabuf_video_plane.cpp
    src/dev/kms_gles_window.cpp
    src/dev/sdl_gles_window.cpp
)

target_link_libraries(glide_dev PUBLIC glide_common)

if(SDL2_FOUND)
    target_compile_definitions(glide_dev PUBLIC OPENHD_GLIDE_HAS_SDL2=1)
    target_include_directories(glide_dev PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(glide_dev PRIVATE ${SDL2_LIBRARIES})
    target_link_directories(glide_dev PRIVATE ${SDL2_LIBRARY_DIRS})
else()
    target_compile_definitions(glide_dev PUBLIC OPENHD_GLIDE_HAS_SDL2=0)
endif()

if(GLESV2_FOUND)
    target_compile_definitions(glide_dev PUBLIC OPENHD_GLIDE_HAS_GLESV2=1)
    target_include_directories(glide_dev PRIVATE ${GLESV2_INCLUDE_DIRS})
    target_link_libraries(glide_dev PRIVATE ${GLESV2_LIBRARIES})
    target_link_directories(glide_dev PRIVATE ${GLESV2_LIBRARY_DIRS})
else()
    target_compile_definitions(glide_dev PUBLIC OPENHD_GLIDE_HAS_GLESV2=0)
endif()

if(LIBDRM_FOUND AND GBM_FOUND AND EGL_FOUND AND GLESV2_FOUND)
    target_compile_definitions(glide_dev PUBLIC OPENHD_GLIDE_HAS_KMS_GBM=1)
    target_include_directories(glide_dev PRIVATE
        ${LIBDRM_INCLUDE_DIRS}
        ${GBM_INCLUDE_DIRS}
        ${EGL_INCLUDE_DIRS}
        ${GLESV2_INCLUDE_DIRS})
    target_link_libraries(glide_dev PRIVATE
        ${LIBDRM_LIBRARIES}
        ${GBM_LIBRARIES}
        ${EGL_LIBRARIES}
        ${GLESV2_LIBRARIES})
    target_link_directories(glide_dev PRIVATE
        ${LIBDRM_LIBRARY_DIRS}
        ${GBM_LIBRARY_DIRS}
        ${EGL_LIBRARY_DIRS}
        ${GLESV2_LIBRARY_DIRS})
else()
    target_compile_definitions(glide_dev PUBLIC OPENHD_GLIDE_HAS_KMS_GBM=0)
endif()

if(MSVC)
    target_compile_options(glide_dev PRIVATE /W4)
    if(OPENHD_GLIDE_WARNINGS_AS_ERRORS)
        target_compile_options(glide_dev PRIVATE /WX)
    endif()
else()
    target_compile_options(glide_dev PRIVATE -Wall -Wextra -Wpedantic)
    if(OPENHD_GLIDE_WARNINGS_AS_ERRORS)
        target_compile_options(glide_dev PRIVATE -Werror)
    endif()
endif()

add_library(glide_flow_core
    src/glide_flow/altitude_widget.cpp
    src/glide_flow/bitmap_font.cpp
    src/glide_flow/fps_counter.cpp
    src/glide_flow/fps_overlay.cpp
    src/glide_flow/gles_text_renderer.cpp
    src/glide_flow/link_overview.cpp
    src/glide_flow/performance_horizon.cpp
    src/glide_flow/simulated_attitude.cpp
    src/glide_flow/speed_widget.cpp
    src/glide_flow/vector_font.cpp
)

target_link_libraries(glide_flow_core PUBLIC glide_common glide_dev)

if(MSVC)
    target_compile_options(glide_flow_core PRIVATE /W4)
    if(OPENHD_GLIDE_WARNINGS_AS_ERRORS)
        target_compile_options(glide_flow_core PRIVATE /WX)
    endif()
else()
    target_compile_options(glide_flow_core PRIVATE -Wall -Wextra -Wpedantic)
    if(OPENHD_GLIDE_WARNINGS_AS_ERRORS)
        target_compile_options(glide_flow_core PRIVATE -Werror)
    endif()
endif()

if(GLESV2_FOUND)
    target_compile_definitions(glide_flow_core PUBLIC OPENHD_GLIDE_HAS_GLESV2=1)
    target_include_directories(glide_flow_core PRIVATE ${GLESV2_INCLUDE_DIRS})
    target_link_libraries(glide_flow_core PRIVATE ${GLESV2_LIBRARIES})
    target_link_directories(glide_flow_core PRIVATE ${GLESV2_LIBRARY_DIRS})
else()
    target_compile_definitions(glide_flow_core PUBLIC OPENHD_GLIDE_HAS_GLESV2=0)
endif()

if(FREETYPE_FOUND)
    target_compile_definitions(glide_flow_core PRIVATE OPENHD_GLIDE_HAS_FREETYPE=1)
    target_include_directories(glide_flow_core PRIVATE ${FREETYPE_INCLUDE_DIRS})
    target_link_libraries(glide_flow_core PRIVATE ${FREETYPE_LIBRARIES})
    target_link_directories(glide_flow_core PRIVATE ${FREETYPE_LIBRARY_DIRS})
else()
    target_compile_definitions(glide_flow_core PRIVATE OPENHD_GLIDE_HAS_FREETYPE=0)
endif()

add_glide_executable(openhd-glide src/openhd_glide/main.cpp)
target_link_libraries(openhd-glide PRIVATE glide_dev)
if(OPENHD_GLIDE_DEVICE_KMS)
    target_compile_definitions(openhd-glide PRIVATE OPENHD_GLIDE_DEVICE_KMS=1)
else()
    target_compile_definitions(openhd-glide PRIVATE OPENHD_GLIDE_DEVICE_KMS=0)
endif()
if(GSTREAMER_FOUND)
    target_compile_definitions(openhd-glide PRIVATE OPENHD_GLIDE_HAS_GSTREAMER=1)
    target_include_directories(openhd-glide PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    target_link_libraries(openhd-glide PRIVATE ${GSTREAMER_LIBRARIES})
    target_link_directories(openhd-glide PRIVATE ${GSTREAMER_LIBRARY_DIRS})
else()
    target_compile_definitions(openhd-glide PRIVATE OPENHD_GLIDE_HAS_GSTREAMER=0)
endif()
add_glide_executable(glide-view src/glide_view/main.cpp)
if(GSTREAMER_FOUND)
    message(STATUS "GStreamer appsink support enabled for glide-view")
    target_compile_definitions(glide-view PRIVATE OPENHD_GLIDE_HAS_GSTREAMER=1)
    target_include_directories(glide-view PRIVATE ${GSTREAMER_INCLUDE_DIRS})
    target_link_libraries(glide-view PRIVATE ${GSTREAMER_LIBRARIES})
    target_link_directories(glide-view PRIVATE ${GSTREAMER_LIBRARY_DIRS})
else()
    message(STATUS "GStreamer appsink/DMABUF support disabled; install gstreamer-1.0, gstreamer-app-1.0, gstreamer-allocators-1.0, and gstreamer-video-1.0 development packages")
    target_compile_definitions(glide-view PRIVATE OPENHD_GLIDE_HAS_GSTREAMER=0)
endif()
add_executable(glide-ui src/glide_ui/main.cpp)
target_link_libraries(glide-ui PRIVATE glide_common)
if(OPENHD_GLIDE_DEVICE_KMS)
    target_compile_definitions(glide-ui PRIVATE OPENHD_GLIDE_DEVICE_KMS=1)
else()
    target_compile_definitions(glide-ui PRIVATE OPENHD_GLIDE_DEVICE_KMS=0)
endif()
if(SDL2_FOUND)
    target_compile_definitions(glide-ui PRIVATE OPENHD_GLIDE_HAS_LVGL_SDL=1)
    target_include_directories(glide-ui PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(glide-ui PRIVATE lvgl ${SDL2_LIBRARIES})
    target_link_directories(glide-ui PRIVATE ${SDL2_LIBRARY_DIRS})
else()
    target_compile_definitions(glide-ui PRIVATE OPENHD_GLIDE_HAS_LVGL_SDL=0)
endif()

add_executable(glide-flow src/glide_flow/main.cpp)
target_link_libraries(glide-flow PRIVATE glide_flow_core glide_dev)
if(OPENHD_GLIDE_DEVICE_KMS)
    target_compile_definitions(glide-flow PRIVATE OPENHD_GLIDE_DEVICE_KMS=1)
else()
    target_compile_definitions(glide-flow PRIVATE OPENHD_GLIDE_DEVICE_KMS=0)
endif()

install(
    TARGETS
        openhd-glide
        glide-view
        glide-flow
        glide-ui
    RUNTIME DESTINATION bin
)
