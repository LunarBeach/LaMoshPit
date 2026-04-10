# Find the FFmpeg libraries that vcpkg installed
find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale)

# Make the include directories and libraries available to the rest of the project
target_include_directories(${PROJECT_NAME} PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE ${FFMPEG_LIBRARIES})