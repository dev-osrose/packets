link_directories(${EXTERNAL_BINARY_DIR})

set(PROJECT "osIROSE-common-core")
project(${PROJECT})

find_package(Threads REQUIRED)

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
    include/json.hpp
    # Networking
    include/platform_defines.h
    include/threadpool.h
    include/network_thread_pool.h
    include/logconsole.h
    include/inetwork.h
    include/cnetwork_asio.h
    )

set(SOURCES_BASE
    src/fuzzy_matching.cpp
    # Networking
    src/logconsole.cpp
    src/network_thread_pool.cpp
    src/cnetwork_asio.cpp
    )

add_library(${PROJECT} STATIC ${SOURCES_BASE} ${SOURCES_HEADER})
target_include_directories(${PROJECT}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_GEN_SRC_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

# ASIO (standalone, header-only)
target_link_libraries(${PROJECT} PUBLIC asio::asio)

# spdlog
target_link_libraries(${PROJECT} PUBLIC spdlog::spdlog)

# Threads
target_link_libraries(${PROJECT} PUBLIC Threads::Threads)

# WinSock (Windows only)
if(WIN32)
    target_link_libraries(${PROJECT} PUBLIC ws2_32 mswsock)
endif()

# OpenSSL / TLS support
if(ENABLE_SSL)
    target_compile_definitions(${PROJECT} PUBLIC USE_SSL)
    target_link_libraries(${PROJECT} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Legacy INTERFACE thread libs (keep for compatibility)
target_link_libraries(${PROJECT}
    INTERFACE
    ${CMAKE_THREAD_LIBS_INIT}
    )

list(TRANSFORM SOURCES_HEADER PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

set_target_properties(${PROJECT} PROPERTIES
    PUBLIC_HEADER "${SOURCES_HEADER}")

set_property(TARGET ${PROJECT} PROPERTY FOLDER "Common")
