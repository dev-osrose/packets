cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

set(PROJECT "osIROSE-common")
project(${PROJECT})

include(ExternalProject)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")
set(CMAKE_GEN_SRC_DIR "${CMAKE_BINARY_DIR}/gen")
set(CMAKE_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/3rdparty" CACHE PATH "Directory where 3rdparty libs will be installed during build time")

set(CMAKE_THIRD_PARTY_BINARY_DIR ${CMAKE_THIRD_PARTY_DIR}/bin)
set(CMAKE_THIRD_PARTY_LIBRARY_DIR ${CMAKE_THIRD_PARTY_DIR}/lib)
set(CMAKE_THIRD_PARTY_INCLUDE_DIR ${CMAKE_THIRD_PARTY_DIR}/include)
set(CMAKE_PREFIX_PATH "${CMAKE_THIRD_PARTY_DIR}")

file(MAKE_DIRECTORY ${CMAKE_THIRD_PARTY_BINARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_THIRD_PARTY_INCLUDE_DIR})
file(MAKE_DIRECTORY ${CMAKE_THIRD_PARTY_LIBRARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_THIRD_PARTY_DIR}/share)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

file(MAKE_DIRECTORY ${CMAKE_THIRD_PARTY_BINARY_DIR})

if (NOT CMAKE_BUILD_TYPE)
  if (OFFICIAL_BUILD)
    set(CMAKE_BUILD_TYPE RelWithDebInfo FORCE)
  elseif (DEBUG)
    set(CMAKE_BUILD_TYPE Debug FORCE)
    add_compiler_flag(-DSPDLOG_TRACE_ON)
  else ()
    set(CMAKE_BUILD_TYPE Release FORCE)
  endif ()

  if (OFFICIAL_DEBUG_BUILD)
    set(CMAKE_BUILD_TYPE Debug FORCE)
  endif ()
endif ()

set(BUILD_TYPE "${CMAKE_BUILD_TYPE}")

if ("${CMAKE_GENERATOR}" MATCHES "Visual Studio")
  set(BUILD_TYPE "$<CONFIG>")
endif ()

find_package(PacketGenerator)
if (NOT PacketGenerator_FOUND)
  include(Build-packet-generator)
endif ()
include(PacketGenerator)

add_subdirectory(src)

install(TARGETS osIROSE-common-core
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  PUBLIC_HEADER DESTINATION include
  INCLUDES DESTINATION src/core/include
)
install(TARGETS osIROSE-common-rosecommon
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  PUBLIC_HEADER DESTINATION include
  INCLUDES DESTINATION src/rosecommon/include ${CMAKE_GEN_SRC_DIR}/packets
)