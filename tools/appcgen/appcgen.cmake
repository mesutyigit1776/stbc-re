# Generates the Appc wrapper sources at build time and adds them to a target.
#
#   include(tools/appcgen/appcgen.cmake)
#   appcgen_add_sources(<target> [OUT_DIR <dir>] [DEFECT <name>])
#
# The output goes to OUT_DIR (default: <build dir>/appc). The folder is also an include directory of the
# target, so the sources can include "appc_wrappers.h", "appc_methods.inc" (rows of the method table) and
# "appc_variables.inc" (rows of the module attributes).
# DEFECT plants a defect in the module tables (wrong_constant or missing_method). The module tests must fail then.

find_package(Python3 COMPONENTS Interpreter REQUIRED)

get_filename_component(APPCGEN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(APPCGEN_INTERFACE_DIR "${APPCGEN_ROOT}/interface")
set(APPCGEN_SCRIPT_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(appcgen_add_sources target)
  cmake_parse_arguments(ARG "" "OUT_DIR;DEFECT" "" ${ARGN})
  if(NOT ARG_OUT_DIR)
    set(ARG_OUT_DIR "${CMAKE_BINARY_DIR}/appc")
  endif()
  set(defect_args "")
  if(ARG_DEFECT)
    set(defect_args --defect "${ARG_DEFECT}")
  endif()

  file(GLOB descriptions CONFIGURE_DEPENDS "${APPCGEN_INTERFACE_DIR}/*.appc")
  set(headers "${ARG_OUT_DIR}/appc_swig.h" "${ARG_OUT_DIR}/appc_wrappers.h"
              "${ARG_OUT_DIR}/appc_methods.inc" "${ARG_OUT_DIR}/appc_variables.inc")
  set(sources "")
  foreach(description IN LISTS descriptions)
    get_filename_component(stem "${description}" NAME_WE)
    list(APPEND sources "${ARG_OUT_DIR}/appc_${stem}.cpp")
  endforeach()
  foreach(module methods constants variables types helpers)
    list(APPEND sources "${ARG_OUT_DIR}/module_${module}.cpp")
  endforeach()

  add_custom_command(
    OUTPUT ${headers} ${sources}
    COMMAND Python3::Interpreter -I "${APPCGEN_SCRIPT_DIR}/appcgen.py"
            --interface "${APPCGEN_INTERFACE_DIR}" --out "${ARG_OUT_DIR}" ${defect_args}
    DEPENDS ${descriptions} "${APPCGEN_SCRIPT_DIR}/appcgen.py" "${APPCGEN_SCRIPT_DIR}/appcdesc.py"
            "${APPCGEN_SCRIPT_DIR}/appcmodule.py"
            "${APPCGEN_ROOT}/target/appc_method_table.tsv" "${APPCGEN_ROOT}/target/appc_constants.tsv"
            "${APPCGEN_ROOT}/target/appc_globals.tsv" "${APPCGEN_ROOT}/target/appc_swig_types.tsv"
            "${APPCGEN_ROOT}/target/script_helpers.tsv"
    COMMENT "Generating Appc wrappers in ${ARG_OUT_DIR}"
    VERBATIM)

  target_sources(${target} PRIVATE ${sources} ${headers})
  target_include_directories(${target} PUBLIC "${ARG_OUT_DIR}")
endfunction()
