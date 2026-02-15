include(FetchContent)

find_program(CARGO_EXECUTABLE cargo REQUIRED)

option(IDL_OFFLINE "Build IDL without network access" OFF)

if(IDL_OFFLINE)
  set(FETCHCONTENT_FULLY_DISCONNECTED ON)
endif()

FetchContent_Declare(
  idl
  GIT_REPOSITORY https://github.com/dev-osrose/IDL.git
  GIT_TAG fc00533026404ee09b99514ce49369c4534740bd
)

FetchContent_MakeAvailable(idl)

set(IDL_TARGET_DIR
  ${idl_BINARY_DIR}/cargo-target
)

set(IDL_PACKET_GENERATOR
  ${IDL_TARGET_DIR}/release/packet_generator${CMAKE_EXECUTABLE_SUFFIX}
)

file(GLOB_RECURSE IDL_RUST_SOURCES
  ${idl_SOURCE_DIR}/src/*.rs
)

add_custom_command(
  OUTPUT ${IDL_PACKET_GENERATOR}

  COMMAND ${CMAKE_COMMAND} -E env
          CARGO_TARGET_DIR=${IDL_TARGET_DIR}
          ${CARGO_EXECUTABLE}
          build --release --locked

  WORKING_DIRECTORY ${idl_SOURCE_DIR}

  DEPENDS
    ${idl_SOURCE_DIR}/Cargo.toml
    ${idl_SOURCE_DIR}/Cargo.lock
    ${IDL_RUST_SOURCES}

  COMMENT "Building Rust packet_generator"
  VERBATIM
)

add_custom_target(idl_packet_generator
  DEPENDS ${IDL_PACKET_GENERATOR}
)

add_executable(utils::packet_generator IMPORTED GLOBAL)

add_dependencies(utils::packet_generator idl_packet_generator)

set_target_properties(utils::packet_generator PROPERTIES
  IMPORTED_LOCATION ${IDL_PACKET_GENERATOR}
)