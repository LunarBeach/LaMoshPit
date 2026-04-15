cmake_minimum_required(VERSION 3.28)
project(LeeAnnesMoshPit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
# OFF avoids "No known features" errors when CMake doesn't recognise the
# exact MSVC version; MSVC still receives /std:c++20 via the standard variable.
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Set Qt paths explicitly
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
set(Qt6_DIR "C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6")
#Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Multimedia MultimediaWidgets Concurrent)
# Pre-find packages that the FFMPEG vcpkg wrapper pulls in as transitive deps.
# Without these explicit calls the wrapper adds targets like mp3lame::mp3lame and
# Iconv::Iconv to FFMPEG_LIBRARIES even when find_package inside the wrapper fails,
# resulting in linker errors.  Setting the _DIR / include / lib hints via CACHE
# FORCE ensures these succeed regardless of system PATH search order.
set(VCPKG_X64_WIN "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/installed/x64-windows")
set(mp3lame_DIR         "${VCPKG_X64_WIN}/share/mp3lame"     CACHE PATH "" FORCE)
set(libopenmpt_DIR      "${VCPKG_X64_WIN}/share/libopenmpt"  CACHE PATH "" FORCE)
set(mpg123_DIR          "${VCPKG_X64_WIN}/share/mpg123"      CACHE PATH "" FORCE)
set(Vorbis_DIR          "${VCPKG_X64_WIN}/share/vorbis"      CACHE PATH "" FORCE)
set(Ogg_DIR             "${VCPKG_X64_WIN}/share/ogg"         CACHE PATH "" FORCE)
set(LibXml2_DIR         "${VCPKG_X64_WIN}/share/libxml2"     CACHE PATH "" FORCE)
set(Iconv_INCLUDE_DIR   "${VCPKG_X64_WIN}/include"           CACHE PATH "" FORCE)
set(Iconv_LIBRARY       "${VCPKG_X64_WIN}/lib/iconv.lib"     CACHE FILEPATH "" FORCE)
find_package(Ogg        CONFIG)
find_package(Vorbis     CONFIG)
find_package(mp3lame    CONFIG)
find_package(mpg123     CONFIG)
find_package(libopenmpt CONFIG)
find_package(LibXml2)
find_package(Iconv)

# Pre-seed FFMPEG per-component cache vars so FindFFMPEG.cmake's NO_DEFAULT_PATH
# search succeeds on a fresh cache (the find_path/find_library calls are guarded
# by if(NOT <var>) so this only takes effect when they haven't been cached yet).
foreach(_ffcomp avcodec avdevice avfilter avformat avutil swresample swscale)
    string(TOUPPER "${_ffcomp}" _FFCOMP)
    set("FFMPEG_lib${_ffcomp}_INCLUDE_DIRS"
        "${VCPKG_X64_WIN}/include"
        CACHE PATH "" FORCE)
    set("FFMPEG_lib${_ffcomp}_LIBRARY_RELEASE"
        "${VCPKG_X64_WIN}/lib/${_ffcomp}.lib"
        CACHE FILEPATH "" FORCE)
    set("FFMPEG_lib${_ffcomp}_LIBRARY_DEBUG"
        "${VCPKG_X64_WIN}/debug/lib/${_ffcomp}d.lib"
        CACHE FILEPATH "" FORCE)
endforeach()
unset(_ffcomp)
unset(_FFCOMP)

#Find FFmpeg (vcpkg)
find_package(FFMPEG REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)

# The vcpkg FFMPEG cmake wrapper unconditionally appends targets like
# libopenmpt::libopenmpt, PkgConfig::modplug, etc. to FFMPEG_LIBRARIES even when
# the corresponding find_package calls fail.  CMake 3.28+ errors at generate time
# if a target with "::" in its name is passed to target_link_libraries but doesn't
# exist.  Strip out any such dangling target names here.
set(FFMPEG_LIBRARIES_CLEAN "")
foreach(_fflib IN LISTS FFMPEG_LIBRARIES)
    if(_fflib MATCHES "::")
        if(TARGET "${_fflib}")
            list(APPEND FFMPEG_LIBRARIES_CLEAN "${_fflib}")
        endif()
    else()
        list(APPEND FFMPEG_LIBRARIES_CLEAN "${_fflib}")
    endif()
endforeach()
unset(_fflib)
# Try to find Multimedia component
find_package(Qt6 COMPONENTS Multimedia QUIET)
if(Qt6Multimedia_FOUND)
    message(STATUS "Qt Multimedia found - enabling video playback")
    set(HAS_MULTIMEDIA TRUE)
else()
    message(STATUS "Qt Multimedia NOT found - using simulated player")
    set(HAS_MULTIMEDIA FALSE)
endif()
# Add h264bitstream as subdirectory
add_subdirectory(h264bitstream)

# ──────────────────────────────────────────────────────────────────────────────
# Spout2 minimal embed — compile only the SpoutDX + dependent SpoutGL helpers
# we need, as a static library.  We don't use Spout2's top-level CMakeLists
# because it pulls in OpenGL targets, examples, installers, and tests that
# aren't relevant to LaMoshPit's bundle.
#
# Output: a single static library `spout2_embed` that LaMoshPit links against.
# ──────────────────────────────────────────────────────────────────────────────
set(SPOUT2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Spout2/SPOUTSDK")
add_library(spout2_embed STATIC
    "${SPOUT2_ROOT}/SpoutDirectX/SpoutDX/SpoutDX.cpp"
    "${SPOUT2_ROOT}/SpoutGL/SpoutCopy.cpp"
    "${SPOUT2_ROOT}/SpoutGL/SpoutDirectX.cpp"
    "${SPOUT2_ROOT}/SpoutGL/SpoutFrameCount.cpp"
    "${SPOUT2_ROOT}/SpoutGL/SpoutSenderNames.cpp"
    "${SPOUT2_ROOT}/SpoutGL/SpoutSharedMemory.cpp"
    "${SPOUT2_ROOT}/SpoutGL/SpoutUtils.cpp"
)
target_include_directories(spout2_embed PUBLIC
    "${SPOUT2_ROOT}/SpoutDirectX/SpoutDX"
    "${SPOUT2_ROOT}/SpoutGL"
)
target_link_libraries(spout2_embed PUBLIC
    d3d11 dxgi shlwapi psapi
)
# Silence warnings from 3rd-party source so they don't drown the build log.
if(MSVC)
    target_compile_options(spout2_embed PRIVATE /W0)
endif()
#Link Directories
link_directories(${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib)

qt_standard_project_setup()

add_executable(LeeAnnesMoshPit WIN32
    main.cpp
    resources.qrc
    gui/MainWindow.cpp
    gui/SettingsDialog.cpp
    gui/BitstreamAnalyzer.cpp
    gui/widgets/TimelineWidget.cpp
    gui/widgets/PreviewPlayer.cpp
    gui/widgets/MacroblockWidget.cpp
    gui/widgets/GlobalParamsWidget.cpp
    gui/widgets/PropertyPanel.cpp
    gui/widgets/BitstreamTestWidget.cpp
    gui/widgets/QuickMoshWidget.cpp
    gui/widgets/ToggleSwitch.cpp
    gui/widgets/MediaBinWidget.cpp
    gui/sequencer/SequencerPreviewPlayer.cpp
    gui/sequencer/SequencerDock.cpp
    gui/sequencer/SequencerClipItem.cpp
    gui/sequencer/SequencerRulerItem.cpp
    gui/sequencer/SequencerPlayheadItem.cpp
    gui/sequencer/SequencerTimelineView.cpp
    gui/sequencer/SequencerTrackHeader.cpp
    gui/sequencer/SequencerTransitionPanel.cpp
    gui/sequencer/SequencerRenderDialog.cpp
    core/pipeline/DecodePipeline.cpp
    core/transform/FrameTransformer.cpp
    core/logger/ControlLogger.cpp
    core/presets/PresetManager.cpp
    core/presets/VersionPathUtil.cpp
    core/project/Project.cpp
    core/sequencer/SequencerProject.cpp
    core/sequencer/EditCommand.cpp
    core/sequencer/HwDecode.cpp
    core/sequencer/SequencerClipDecoder.cpp
    core/sequencer/SequencerPlaybackClock.cpp
    core/sequencer/TrackPlaybackChain.cpp
    core/sequencer/Transition.cpp
    core/sequencer/FrameRouter.cpp
    core/sequencer/SpoutSender.cpp
    core/sequencer/SequencerRenderer.cpp
    core/util/HwEncoder.cpp
    core/thumbnail/ThumbnailGenerator.cpp
)

#Link everything together
target_link_libraries(LeeAnnesMoshPit PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Concurrent
    h264bitstream
    spout2_embed
    # Uses the official targets provided by vcpkg (dangling :: targets stripped above)
    ${FFMPEG_LIBRARIES_CLEAN}
    # Core FFMPEG libs — linked explicitly so they are always present regardless of
    # whether FindFFMPEG.cmake succeeds on a fresh cache.
    debug     "${VCPKG_X64_WIN}/debug/lib/avcodec.lib"
    optimized "${VCPKG_X64_WIN}/lib/avcodec.lib"
    debug     "${VCPKG_X64_WIN}/debug/lib/avformat.lib"
    optimized "${VCPKG_X64_WIN}/lib/avformat.lib"
    debug     "${VCPKG_X64_WIN}/debug/lib/avutil.lib"
    optimized "${VCPKG_X64_WIN}/lib/avutil.lib"
    debug     "${VCPKG_X64_WIN}/debug/lib/swscale.lib"
    optimized "${VCPKG_X64_WIN}/lib/swscale.lib"
    debug     "${VCPKG_X64_WIN}/debug/lib/swresample.lib"
    optimized "${VCPKG_X64_WIN}/lib/swresample.lib"
    # LaMoshPit-Edge: our forked libx264 (x264-lamoshpit vcpkg overlay port).
    # Provides bitstream-surgery hooks in x264_image_properties_t for per-MB
    # overrides (CBP zero, force skip, force MB type, force intra mode,
    # MVD injection, DCT scale).  FrameTransformer::runBitstreamEdit() calls
    # x264_encoder_encode directly, bypassing FFmpeg's libx264 wrapper, to
    # populate pic.prop.*_override fields with per-MB data from FrameMBParams.
    debug     "${VCPKG_X64_WIN}/debug/lib/libx264.lib"
    optimized "${VCPKG_X64_WIN}/lib/libx264.lib"
)
# Add Multimedia if found
if(HAS_MULTIMEDIA)
    target_link_libraries(LeeAnnesMoshPit PRIVATE Qt6::Multimedia)
endif()

target_include_directories(LeeAnnesMoshPit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/h264bitstream
       # These should be automatic but you can add if needed:
    "C:/Qt/6.11.0/msvc2022_64/include"
    "C:/Qt/6.11.0/msvc2022_64/include/QtMultimediaWidgets"
    ${FFMPEG_INCLUDE_DIRS}
)

if(WIN32)
    target_link_options(LeeAnnesMoshPit PRIVATE /SUBSYSTEM:WINDOWS)
endif()