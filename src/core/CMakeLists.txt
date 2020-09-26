link_directories(${EXTERNAL_BINARY_DIR})

set(PROJECT "osIROSE-common-core")
project(${PROJECT})

set(SOURCES_HEADER
    include/array_wrapper.h
    include/singleton.h
    include/factory.h
    include/hash.h
    include/fuzzy_matching.h
    include/random.h
    include/mwsrqueue.h
    include/enumerate.h
    include/transform.h
    include/fire_once.h
    include/tuple_index.h
    include/tuple_iterate.h
    )

set(SOURCES_BASE
    src/fuzzy_matching.cpp
    )

add_library(${PROJECT} STATIC ${SOURCES_BASE} ${SOURCES_HEADER})
target_include_directories(${PROJECT}
    PUBLIC
    $<INSTALL_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_GEN_SRC_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

target_link_libraries(${PROJECT}
    INTERFACE
    ${CMAKE_THREAD_LIBS_INIT}
    )

set_property(TARGET ${PROJECT} PROPERTY FOLDER "Common")