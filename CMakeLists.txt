cmake_minimum_required(VERSION 3.12.4 FATAL_ERROR)
set(PROJECT Packets)
project(${PROJECT})

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules")

find_package(PacketGenerator)

set(IDL_FILES
    packet.xml
)

set(args
  TARGET RosePackets
  IDLFILES ${IDL_FILES}
  IDLROOT ${CMAKE_CURRENT_SOURCE_DIR}/packets
  SRC_OUTPATH ${CMAKE_GEN_SRC_DIR}/packets
  HDR_OUTPATH ${CMAKE_GEN_SRC_DIR}/packets
)

generate_packets(PACKET_SOURCES PACKET_HEADERS ${args})

add_library(${PROJECT} STATIC ${SOURCES_BASE} ${PACKET_SOURCES} ${PACKET_HEADERS} ${SOURCES_HEADER})
target_include_directories(${PROJECT}
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_GEN_SRC_DIR}/packets>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if (NOT MSVC)
  target_compile_options(${PROJECT} PRIVATE -Werror)
endif()

add_dependencies(${PROJECT} RosePackets_generated)


source_group(TREE ${CMAKE_GEN_SRC_DIR}/packets PREFIX "Packet Source Files" FILES ${PACKET_SOURCES})
source_group(TREE ${CMAKE_GEN_SRC_DIR}/packets PREFIX "Packet Header Files" FILES ${PACKET_HEADERS})