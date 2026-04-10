# Install script for directory: C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/LeeAnnesMoshPit")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/Debug/h264bitstream.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/Release/h264bitstream.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/MinSizeRel/h264bitstream.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/RelWithDebInfo/h264bitstream.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/bs.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_avcc.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_sei.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_stream.h"
      )
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/bs.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_avcc.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_sei.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_stream.h"
      )
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/bs.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_avcc.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_sei.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_stream.h"
      )
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE FILES
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/bs.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_avcc.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_sei.h"
      "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/h264bitstream/h264_stream.h"
      )
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/Debug/h264_analyze.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/Release/h264_analyze.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/MinSizeRel/h264_analyze.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/RelWithDebInfo/h264_analyze.exe")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/Debug/svc_split.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/Release/svc_split.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/MinSizeRel/svc_split.exe")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "C:/Users/Thelu/Desktop/CodingProjects/LaMoshPit/build/h264bitstream/RelWithDebInfo/svc_split.exe")
  endif()
endif()

