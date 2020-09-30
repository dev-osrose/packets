if( DISABLE_CRYPT )
  add_compiler_flags(-DDISABLE_CRYPT)
endif()

include_directories(
  SYSTEM ${CMAKE_THIRD_PARTY_INCLUDE_DIR}
)

link_directories(
  ${CMAKE_THIRD_PARTY_LIBRARY_DIR}
)

add_subdirectory(core)
add_subdirectory(rosecommon)