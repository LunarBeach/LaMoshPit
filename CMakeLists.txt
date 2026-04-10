cmake_minimum_required(VERSION 3.28)
project(LeeAnnesMoshPit LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Set Qt paths explicitly
set(CMAKE_PREFIX_PATH "C:/Qt/6.11.0/msvc2022_64")
set(Qt6_DIR "C:/Qt/6.11.0/msvc2022_64/lib/cmake/Qt6")
#Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Multimedia MultimediaWidgets Concurrent)
#Find FFmpeg (vcpkg)
find_package(FFMPEG REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)
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
#Link Directories
link_directories(${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib)

qt_standard_project_setup()

add_executable(LeeAnnesMoshPit WIN32
    main.cpp
    gui/MainWindow.cpp
    gui/BitstreamAnalyzer.cpp 
    gui/widgets/TimelineWidget.cpp
    gui/widgets/PreviewPlayer.cpp
    gui/widgets/PropertyPanel.cpp
    gui/widgets/BitstreamTestWidget.cpp 
    core/pipeline/DecodePipeline.cpp
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
    # Uses the official targets provided by vcpkg
    ${FFMPEG_LIBRARIES}
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