# MSYS2/MinGW toolchain sanity test — builds a minimal Qt6 + MLT exe.
# Configure + build from the MSYS2 MINGW64 shell:
#   cd scripts/nle-sanity-test
#   cmake -G Ninja -B build
#   cmake --build build
#   ./build/nle-test.exe
#
# Expected: a small Qt window pops up showing the detected MLT version.

cmake_minimum_required(VERSION 3.22)
project(nle-sanity-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MLTPP REQUIRED IMPORTED_TARGET mlt++-7>=7.36.0)

add_executable(nle-test WIN32 main.cpp)
target_link_libraries(nle-test PRIVATE
    Qt6::Widgets
    PkgConfig::MLTPP
)

# WIN32 subsystem suppresses the console window; revert to console while
# debugging by building with -DCMAKE_EXE_LINKER_FLAGS="-Wl,--subsystem,console".
