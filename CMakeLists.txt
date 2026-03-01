cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

project(osIROSE-common)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_GEN_SRC_DIR
  ${CMAKE_BINARY_DIR}/gen
)

list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
)

include(idl-tool)
include(idl-generate)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Networking options
# ---------------------------------------------------------------------------
option(ENABLE_SSL "Enable TLS/SSL support via OpenSSL and asio::ssl" OFF)

# ---------------------------------------------------------------------------
# ASIO (standalone, header-only — no Boost)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-30-2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(asio)

if(NOT TARGET asio::asio)
    add_library(asio::asio INTERFACE IMPORTED GLOBAL)
    target_include_directories(asio::asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio::asio INTERFACE
        ASIO_STANDALONE
        _WIN32_WINNT=0x0601
    )
endif()

# ---------------------------------------------------------------------------
# spdlog
# ---------------------------------------------------------------------------
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.14.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spdlog)

# ---------------------------------------------------------------------------
# OpenSSL (only required when SSL is enabled)
# ---------------------------------------------------------------------------
if(ENABLE_SSL)
    find_package(OpenSSL REQUIRED)
endif()

add_subdirectory(src)

install(TARGETS osIROSE-common-core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)

install(TARGETS osIROSE-common-rosecommon
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    PUBLIC_HEADER DESTINATION include
)