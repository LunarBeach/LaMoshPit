# =============================================================================
# LaMoshPit_NLE.exe — MinGW/MSYS2-side build for the NLE sequencer process.
#
# CRITICAL: this CMakeLists.txt is the top-level for the NLE executable.
# It is NEVER added via add_subdirectory() from the top-level LaMoshPit
# CMakeLists.txt (which is MSVC).  The two builds are fully isolated:
#
#   LaMoshPit.exe     ← built with MSVC + vcpkg   (top-level CMakeLists.txt)
#   LaMoshPit_NLE.exe ← built with MSYS2/MinGW    (this CMakeLists.txt)
#
# They share no source, no toolchain, and no link targets.  They meet only
# at install-time in the deployment tree.  See
#   docs/Implementation_Plan_NLE_Rebuild_2026-04-17.md
# Step 2 for the rationale.
#
# Build invocation (from MSYS2 MINGW64 shell):
#   cd /c/Users/Thelu/Desktop/CodingProjects/LaMoshPit/nle
#   cmake -G Ninja -B build
#   cmake --build build
#
# Or via the wrapper:
#   bash scripts/build-nle-msys2.sh
# =============================================================================

cmake_minimum_required(VERSION 3.22)
project(LaMoshPit_NLE LANGUAGES CXX)

# Guard: refuse to configure from MSVC.  If someone accidentally runs
# cmake -B build with a Visual Studio generator, bail out with a clear
# message rather than silently producing a broken VS project.
if(MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR
        "nle/CMakeLists.txt must be configured from MSYS2 MINGW64, not MSVC. "
        "Launch the MSYS2 MINGW64 shell and run:\n"
        "  cd $(repo)/nle && cmake -G Ninja -B build")
endif()

# Sanity: we want the MinGW C++ compiler, not gcc masquerading as cc.
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING
        "NLE expected GNU (MinGW) C++ compiler but found "
        "${CMAKE_CXX_COMPILER_ID}.  Build will continue but may fail.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Default to Release if not specified.  Debug works too; pass
# -DCMAKE_BUILD_TYPE=Debug to the configure step.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Dependencies ────────────────────────────────────────────────────────────
find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Quick
    QuickControls2
    QuickWidgets
    Qml
    Multimedia
)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=7.36.0)

# ── Executable ──────────────────────────────────────────────────────────────
add_executable(LaMoshPit_NLE WIN32
    src/main.cpp
)

target_include_directories(LaMoshPit_NLE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(LaMoshPit_NLE PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickWidgets
    Qt6::Qml
    Qt6::Multimedia
    PkgConfig::MLTPP
)

# ── Windows deployment ──────────────────────────────────────────────────────
# Running the exe directly from build/ requires Qt DLLs on PATH.  For
# installs we'll use windeployqt-style bundling in Phase 1 Step 12.
# For now, devs run from the MINGW64 shell where /mingw64/bin/*.dll is
# already on PATH.
if(WIN32)
    # Silences the benign "ignored unrecognised -Wl,--subsystem flag"
    # warning that appears with WIN32 executables on some MinGW versions.
    set_target_properties(LaMoshPit_NLE PROPERTIES WIN32_EXECUTABLE ON)
endif()

message(STATUS "LaMoshPit_NLE configured:")
message(STATUS "  C++ compiler : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Qt6          : ${Qt6_VERSION}")
message(STATUS "  MLT++        : ${MLTPP_VERSION}")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
