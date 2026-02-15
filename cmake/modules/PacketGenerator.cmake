function(target_generate_packets TARGET)
  cmake_parse_arguments(ARG
    ""
    "IDLROOT;OUTDIR"
    "IDLFILES"
    ${ARGN}
  )

  if(NOT TARGET ${TARGET})
    message(FATAL_ERROR
      "target_generate_packets: '${TARGET}' is not a valid target"
    )
  endif()

  if(NOT ARG_IDLFILES)
    message(FATAL_ERROR
      "target_generate_packets: no IDLFILES provided"
    )
  endif()

  if(NOT ARG_IDLROOT)
    message(FATAL_ERROR
      "target_generate_packets: IDLROOT is required"
    )
  endif()

  # Default output dir (per-config safe)
  if(NOT ARG_OUTDIR)
    set(ARG_OUTDIR
      ${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>
    )
  endif()

  set(SRC_OUTPATH ${ARG_OUTDIR}/src)
  set(HDR_OUTPATH ${ARG_OUTDIR}/include)

  set(local_srcs)
  set(local_hdrs)

  foreach(idl_file IN LISTS ARG_IDLFILES)

    if(NOT idl_file MATCHES "\\.xml$")
      message(FATAL_ERROR
        "IDL file '${idl_file}' must end with .xml"
      )
    endif()

    get_filename_component(abs_file "${idl_file}" ABSOLUTE)

    file(RELATIVE_PATH rel_path
      "${ARG_IDLROOT}"
      "${abs_file}"
    )

    if(rel_path MATCHES "^\\.\\.")
      message(FATAL_ERROR
        "IDL file '${idl_file}' is not under IDLROOT "
        "'${ARG_IDLROOT}'"
      )
    endif()

    string(REGEX REPLACE "\\.xml$" ""
      rel_no_ext "${rel_path}"
    )

    set(cxx_file
      "${SRC_OUTPATH}/${rel_no_ext}.cpp"
    )

    set(h_file
      "${HDR_OUTPATH}/${rel_no_ext}.h"
    )

    list(APPEND local_srcs "${cxx_file}")
    list(APPEND local_hdrs "${h_file}")

    add_custom_command(
      OUTPUT "${cxx_file}" "${h_file}"

      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${SRC_OUTPATH}"
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "${HDR_OUTPATH}"

      COMMAND $<TARGET_FILE:utils::packet_generator>
              --inputs "${abs_file}"
              cpp
              --output-header-folder "${HDR_OUTPATH}"
              --output-source-folder "${SRC_OUTPATH}"

      DEPENDS
        "${abs_file}"
        utils::packet_generator

      COMMENT "Generating packets from ${idl_file}"
      VERBATIM
    )
  endforeach()

  set_source_files_properties(
    ${local_srcs}
    ${local_hdrs}
    PROPERTIES GENERATED TRUE
  )

  target_sources(${TARGET}
    PRIVATE
      ${local_srcs}
      ${local_hdrs}
  )

  target_include_directories(${TARGET}
    PRIVATE
      ${HDR_OUTPATH}
  )

  add_dependencies(${TARGET} utils::packet_generator)
endfunction()