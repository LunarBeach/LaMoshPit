# =============================================================================
# x264-lamoshpit — overlay port for LaMoshPit-Edge's modified x264 fork.
#
# Unlike vcpkg's standard x264 port (which downloads fresh source from
# code.videolan.org), this port uses the local x264 source tree committed into
# the LaMoshPit repo at ../../x264-fork/. That tree contains our per-MB CBP
# override hooks on top of vanilla x264.
#
# The build steps are identical to vcpkg's standard x264 port — we apply the
# same MSVC-compat patches and use the same configure+make options.
# =============================================================================

# Locate our modified x264 source tree relative to this overlay port's location.
# CURRENT_PORT_DIR = vcpkg-overlay-ports/x264-lamoshpit
# We need ../../x264-fork relative to the repo root.
get_filename_component(OVERLAY_ROOT "${CURRENT_PORT_DIR}/../../" ABSOLUTE)
set(X264_LOCAL_SOURCE "${OVERLAY_ROOT}/x264-fork")

if(NOT EXISTS "${X264_LOCAL_SOURCE}/x264.h")
    message(FATAL_ERROR
        "x264-lamoshpit: expected modified x264 source tree at:\n"
        "  ${X264_LOCAL_SOURCE}\n"
        "but could not find x264.h there. Check that x264-fork/ is present in the repo root.")
endif()

message(STATUS "x264-lamoshpit: using local source at ${X264_LOCAL_SOURCE}")

# Copy the local source into vcpkg's build tree so it can be patched/configured
# without touching our committed source.
set(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/src/x264-lamoshpit-local")
file(REMOVE_RECURSE "${SOURCE_PATH}")
file(COPY "${X264_LOCAL_SOURCE}/" DESTINATION "${SOURCE_PATH}")

# Apply the same MSVC-compat patches vcpkg uses for stock x264.
vcpkg_apply_patches(
    SOURCE_PATH "${SOURCE_PATH}"
    PATCHES
        uwp-cflags.patch
        parallel-install.patch
        allow-clang-cl.patch
        configure.patch
)

# Helper: cross-compile prefix detection (copied verbatim from stock x264 port).
function(add_cross_prefix)
  if(configure_env MATCHES "CC=([^\/]*-)gcc$")
      vcpkg_list(APPEND arg_OPTIONS "--cross-prefix=${CMAKE_MATCH_1}")
  endif()
  set(arg_OPTIONS "${arg_OPTIONS}" PARENT_SCOPE)
endfunction()

set(nasm_archs x86 x64)
set(gaspp_archs arm arm64)
if(NOT "asm" IN_LIST FEATURES)
    vcpkg_list(APPEND OPTIONS --disable-asm)
elseif(NOT "$ENV{AS}" STREQUAL "")
    # Accept setting from triplet
elseif(VCPKG_TARGET_ARCHITECTURE IN_LIST nasm_archs)
    vcpkg_find_acquire_program(NASM)
    vcpkg_insert_program_into_path("${NASM}")
    set(ENV{AS} "${NASM}")
elseif(VCPKG_TARGET_ARCHITECTURE IN_LIST gaspp_archs AND VCPKG_TARGET_IS_WINDOWS AND VCPKG_HOST_IS_WINDOWS)
    vcpkg_find_acquire_program(GASPREPROCESSOR)
    list(FILTER GASPREPROCESSOR INCLUDE REGEX gas-preprocessor)
    file(INSTALL "${GASPREPROCESSOR}" DESTINATION "${SOURCE_PATH}/tools" RENAME "gas-preprocessor.pl")
endif()

vcpkg_list(SET OPTIONS_RELEASE)
if("tool" IN_LIST FEATURES)
    vcpkg_list(APPEND OPTIONS_RELEASE --enable-cli)
else()
    vcpkg_list(APPEND OPTIONS_RELEASE --disable-cli)
endif()

if("chroma-format-all" IN_LIST FEATURES)
    vcpkg_list(APPEND OPTIONS --chroma-format=all)
endif()

if(NOT "gpl" IN_LIST FEATURES)
    vcpkg_list(APPEND OPTIONS --disable-gpl)
endif()

if(VCPKG_TARGET_IS_UWP)
    list(APPEND OPTIONS --extra-cflags=-D_WIN32_WINNT=0x0A00)
endif()

vcpkg_make_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    DISABLE_CPPFLAGS
    DISABLE_MSVC_WRAPPERS
    LANGUAGES ASM C CXX
    DISABLE_MSVC_TRANSFORMATIONS
    PRE_CONFIGURE_CMAKE_COMMANDS
        add_cross_prefix
    OPTIONS
        ${OPTIONS}
        --enable-pic
        --disable-lavf
        --disable-swscale
        --disable-avs
        --disable-ffms
        --disable-gpac
        --disable-lsmash
        --disable-bashcompletion
    OPTIONS_RELEASE
        ${OPTIONS_RELEASE}
        --enable-strip
        "--bindir=\\\${prefix}/bin"
    OPTIONS_DEBUG
        --enable-debug
        --disable-cli
        "--bindir=\\\${prefix}/bin"
)

vcpkg_make_install()

if("tool" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES x264 AUTO_CLEAN)
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/lib/pkgconfig/x264.pc" "-lx264" "-llibx264")
    if(NOT VCPKG_BUILD_TYPE)
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/debug/lib/pkgconfig/x264.pc" "-lx264" "-llibx264")
    endif()
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "dynamic" AND VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)
    file(RENAME "${CURRENT_PACKAGES_DIR}/lib/libx264.dll.lib" "${CURRENT_PACKAGES_DIR}/lib/libx264.lib")
    if (NOT VCPKG_BUILD_TYPE)
        file(RENAME "${CURRENT_PACKAGES_DIR}/debug/lib/libx264.dll.lib" "${CURRENT_PACKAGES_DIR}/debug/lib/libx264.lib")
    endif()
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/x264.h" "#ifdef X264_API_IMPORTS" "#if 1")
elseif(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/x264.h" "defined(U_STATIC_IMPLEMENTATION)" "1" IGNORE_UNCHANGED)
    file(REMOVE_RECURSE
        "${CURRENT_PACKAGES_DIR}/bin"
        "${CURRENT_PACKAGES_DIR}/debug/bin"
    )
endif()

vcpkg_fixup_pkgconfig()

vcpkg_copy_pdbs()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
