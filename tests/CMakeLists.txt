include(GoogleTest)

# One binary per test file, on purpose: NetworkThreadPool is a singleton owning
# an io_context and a thread pool, and DeleteInstance() is only safe once every
# socket built against it is gone. A process per file keeps that survivable.
#
# The TLS tests also bind loopback ports, so keeping them out of the same
# process as anything else avoids cross-test interference.
macro(osirose_common_test name)
  set(TEST_NAME Test${name})

  add_executable(${TEST_NAME} ${ARGN})

  target_include_directories(${TEST_NAME}
      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
  )

  target_link_libraries(${TEST_NAME}
      PRIVATE
      osIROSE-common-core
      GTest::gtest
      GTest::gtest_main
  )

  gtest_discover_tests(${TEST_NAME})

  set_property(TARGET ${TEST_NAME} PROPERTY FOLDER "Tests")
endmacro()

# Both files compile in a non-SSL build too, where they report as skipped rather
# than passing vacuously. That is deliberate: a green run against a build that
# compiled the whole TLS implementation out is not evidence of anything.
osirose_common_test(SslConfig    test_ssl_config.cpp)
osirose_common_test(SslHandshake test_ssl_handshake.cpp)

# tls_test_certs.h calls the OpenSSL API directly to mint throwaway
# certificates. osIROSE-common-core only links OpenSSL when ENABLE_SSL is on,
# and the header is #ifdef'd to match, so nothing extra is needed here.
