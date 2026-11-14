# CPython 1.5.2 core as a static library, plus a start-up check. No dynamic loading.

set(PY152_DIR ${CMAKE_CURRENT_LIST_DIR}/Python-1.5.2)
set(PYHOST_DIR ${CMAKE_CURRENT_LIST_DIR}/../src/pyhost)

set(PY152_SOURCES
  ${PY152_DIR}/Objects/abstract.c ${PY152_DIR}/Objects/bufferobject.c ${PY152_DIR}/Objects/classobject.c
  ${PY152_DIR}/Objects/cobject.c ${PY152_DIR}/Objects/complexobject.c ${PY152_DIR}/Objects/dictobject.c
  ${PY152_DIR}/Objects/fileobject.c ${PY152_DIR}/Objects/floatobject.c ${PY152_DIR}/Objects/frameobject.c
  ${PY152_DIR}/Objects/funcobject.c ${PY152_DIR}/Objects/intobject.c ${PY152_DIR}/Objects/listobject.c
  ${PY152_DIR}/Objects/longobject.c ${PY152_DIR}/Objects/methodobject.c ${PY152_DIR}/Objects/moduleobject.c
  ${PY152_DIR}/Objects/object.c ${PY152_DIR}/Objects/rangeobject.c ${PY152_DIR}/Objects/sliceobject.c
  ${PY152_DIR}/Objects/stringobject.c ${PY152_DIR}/Objects/tupleobject.c ${PY152_DIR}/Objects/typeobject.c

  ${PY152_DIR}/Parser/acceler.c ${PY152_DIR}/Parser/bitset.c ${PY152_DIR}/Parser/grammar1.c
  ${PY152_DIR}/Parser/intrcheck.c ${PY152_DIR}/Parser/listnode.c ${PY152_DIR}/Parser/metagrammar.c
  ${PY152_DIR}/Parser/myreadline.c ${PY152_DIR}/Parser/node.c ${PY152_DIR}/Parser/parser.c
  ${PY152_DIR}/Parser/parsetok.c ${PY152_DIR}/Parser/tokenizer.c

  ${PY152_DIR}/Python/bltinmodule.c ${PY152_DIR}/Python/ceval.c ${PY152_DIR}/Python/compile.c
  ${PY152_DIR}/Python/errors.c ${PY152_DIR}/Python/frozen.c ${PY152_DIR}/Python/getargs.c
  ${PY152_DIR}/Python/getcompiler.c ${PY152_DIR}/Python/getcopyright.c ${PY152_DIR}/Python/getmtime.c
  ${PY152_DIR}/Python/getplatform.c ${PY152_DIR}/Python/getversion.c ${PY152_DIR}/Python/graminit.c
  ${PY152_DIR}/Python/import.c ${PY152_DIR}/Python/importdl.c ${PY152_DIR}/Python/marshal.c
  ${PY152_DIR}/Python/modsupport.c ${PY152_DIR}/Python/mystrtoul.c ${PY152_DIR}/Python/pyfpe.c
  ${PY152_DIR}/Python/pystate.c ${PY152_DIR}/Python/pythonrun.c ${PY152_DIR}/Python/sigcheck.c ${PY152_DIR}/Python/structmember.c
  ${PY152_DIR}/Python/sysmodule.c ${PY152_DIR}/Python/thread.c ${PY152_DIR}/Python/traceback.c

  ${PY152_DIR}/Modules/_localemodule.c ${PY152_DIR}/Modules/arraymodule.c ${PY152_DIR}/Modules/binascii.c ${PY152_DIR}/Modules/cmathmodule.c
  ${PY152_DIR}/Modules/cPickle.c ${PY152_DIR}/Modules/cStringIO.c ${PY152_DIR}/Modules/errnomodule.c
  ${PY152_DIR}/Modules/getbuildinfo.c ${PY152_DIR}/PC/getpathp.c ${PY152_DIR}/Modules/mathmodule.c ${PY152_DIR}/Modules/newmodule.c
  ${PY152_DIR}/Modules/operator.c ${PY152_DIR}/Modules/posixmodule.c ${PY152_DIR}/Modules/regexmodule.c ${PY152_DIR}/Modules/regexpr.c
  ${PY152_DIR}/Modules/stropmodule.c ${PY152_DIR}/Modules/structmodule.c ${PY152_DIR}/Modules/threadmodule.c
  ${PY152_DIR}/Modules/timemodule.c

  ${PYHOST_DIR}/config.c
  ${PYHOST_DIR}/long32.c
  ${PYHOST_DIR}/securelevel.c
  ${PYHOST_DIR}/nt_platform.c
  ${PYHOST_DIR}/win32_stub.c
)

# The sources are K&R C of 1999: C89 mode, and no warnings of the kinds that the old style code always causes.
# -fwrapv: the exe wraps signed integer results. The cast and format warnings find places where a pointer or a 64-bit host
# value meets the 32-bit long (E0163).
set(PY152_C_FLAGS
  -std=gnu89 -ffp-contract=off -fwrapv -Wformat -Wpointer-to-int-cast -Wint-to-pointer-cast -Wint-conversion
  -Wno-deprecated-non-prototype -Wno-missing-field-initializers -Wno-unused-parameter -Wno-unused-variable
  -Wno-unused-but-set-variable -Wno-sign-compare -Wno-parentheses -Wno-cast-function-type-mismatch
  -Wno-macro-redefined -Wno-pointer-bool-conversion -Wno-unused-function -Wno-incompatible-function-pointer-types -Wno-sometimes-uninitialized -Wno-incompatible-pointer-types-discards-qualifiers)

# fileobject.c defines a static getline(), which clashes with POSIX getline() in <stdio.h>.
set_source_files_properties(${PY152_DIR}/Objects/fileobject.c PROPERTIES COMPILE_OPTIONS "-include;${PYHOST_DIR}/fileobject_prefix.h")

# The interpreter files that open files by their own paths use the Windows name mapping (E0114, E0122).
foreach(F Python/bltinmodule.c Python/pythonrun.c Python/traceback.c)
  set_source_files_properties(${PY152_DIR}/${F} PROPERTIES COMPILE_OPTIONS "-include;${PYHOST_DIR}/hostio_prefix.h")
endforeach()
set_source_files_properties(${PY152_DIR}/Python/import.c PROPERTIES COMPILE_OPTIONS "-include;${PYHOST_DIR}/import_case_prefix.h")
# PC/getpathp.c is the Win32 version of the path code. The host gives it stand-ins for the Win32 calls.
set_source_files_properties(${PY152_DIR}/PC/getpathp.c PROPERTIES
  COMPILE_OPTIONS "-DMS_WIN32;-I${PYHOST_DIR}/win32;-include;${PYHOST_DIR}/hostio_prefix.h")

# The files that make or read float text use the float conversions of the C run time of the exe (E0155 to E0160).
foreach(F Objects/floatobject.c Objects/complexobject.c Objects/stringobject.c Objects/abstract.c Modules/cPickle.c
          Modules/stropmodule.c Python/bltinmodule.c Python/compile.c Python/marshal.c)
  set_property(SOURCE ${PY152_DIR}/${F} APPEND PROPERTY COMPILE_OPTIONS "-include;${PYHOST_DIR}/crt_float_prefix.h")
endforeach()

# A fixed build date keeps the build reproducible.
set_source_files_properties(${PY152_DIR}/Modules/getbuildinfo.c PROPERTIES COMPILE_DEFINITIONS "DATE=\"none\";TIME=\"none\"")

add_library(bc_python152 STATIC ${PY152_SOURCES})
target_include_directories(bc_python152 PUBLIC ${PYHOST_DIR} ${PY152_DIR}/Include ${PY152_DIR}/Parser)
target_compile_options(bc_python152 PRIVATE ${PY152_C_FLAGS})
target_compile_definitions(bc_python152 PRIVATE HAVE_CONFIG_H=1)
target_link_libraries(bc_python152 PUBLIC bc_engine)
set_target_properties(bc_python152 PROPERTIES C_STANDARD 90 C_EXTENSIONS ON)

option(BC_PY_SELFTEST_DEFECT "Make the Python start-up check expect a wrong value (the check must then fail)" OFF)

add_executable(bc_python152_selftest ${PYHOST_DIR}/selftest.c)
target_link_libraries(bc_python152_selftest PRIVATE bc_python152)
target_compile_options(bc_python152_selftest PRIVATE ${PY152_C_FLAGS})
# A generated header carries the option, so that a change of the option always rebuilds the check.
configure_file(${PYHOST_DIR}/selftest_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/selftest_config.h)
target_include_directories(bc_python152_selftest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

enable_testing()
add_test(NAME python152_selftest COMMAND bc_python152_selftest)
add_custom_target(check_python152 COMMAND bc_python152_selftest DEPENDS bc_python152_selftest)

# Checks of the behaviour that stbc.exe adds to the interpreter. Each group is a script in tests/py152.
# BC_PY_EXETEST_DEFECT names a group; the checks of that group must then all fail.
set(BC_PY_EXETEST_DEFECT "" CACHE STRING "Group of the Python behaviour checks that must fail")
add_executable(bc_python152_exetest ${PYHOST_DIR}/exetest.c)
target_link_libraries(bc_python152_exetest PRIVATE bc_python152)
target_compile_options(bc_python152_exetest PRIVATE ${PY152_C_FLAGS})
target_compile_definitions(bc_python152_exetest PRIVATE "BC_PY_EXETEST_DEFECT=\"${BC_PY_EXETEST_DEFECT}\"")

set(BC_PY_EXETEST_GROUPS import securelevel nt locale modules path floats ints)
set(BC_PY_EXETEST_COMMANDS)
foreach(GROUP ${BC_PY_EXETEST_GROUPS})
  set(GAME_DIR ${CMAKE_BINARY_DIR}/pytest_game_${GROUP})
  set(SCRIPT ${CMAKE_CURRENT_LIST_DIR}/../tests/py152/${GROUP}.py)
  add_test(NAME python152_exe_${GROUP} COMMAND bc_python152_exetest ${GAME_DIR} ${SCRIPT})
  list(APPEND BC_PY_EXETEST_COMMANDS COMMAND bc_python152_exetest ${GAME_DIR} ${SCRIPT})
endforeach()
add_custom_target(check_python152_exe ${BC_PY_EXETEST_COMMANDS} DEPENDS bc_python152_exetest)

# The probe library of the emulator tests of the 32-bit long (tests/emu/tests/py_long.py).
add_library(bc_pylong_probe SHARED ${PYHOST_DIR}/long32_probe.c)
target_link_libraries(bc_pylong_probe PRIVATE bc_python152)
target_compile_options(bc_pylong_probe PRIVATE ${PY152_C_FLAGS})

# A copy of the interpreter with the long of the host. The emulator tests use it as a planted defect: they must fail on it.
add_library(bc_python152_wide STATIC ${PY152_SOURCES})
target_include_directories(bc_python152_wide PUBLIC ${PYHOST_DIR} ${PY152_DIR}/Include ${PY152_DIR}/Parser)
target_compile_options(bc_python152_wide PRIVATE ${PY152_C_FLAGS})
target_compile_definitions(bc_python152_wide PUBLIC HAVE_CONFIG_H=1 BC_PY_WIDE_LONG=1)
target_link_libraries(bc_python152_wide PUBLIC bc_engine)
set_target_properties(bc_python152_wide PROPERTIES C_STANDARD 90 C_EXTENSIONS ON)

add_library(bc_pylong_probe_wide SHARED ${PYHOST_DIR}/long32_probe.c)
target_link_libraries(bc_pylong_probe_wide PRIVATE bc_python152_wide)
target_compile_options(bc_pylong_probe_wide PRIVATE ${PY152_C_FLAGS})

# The integer checks run on the interpreter with the long of the host. They must fail there (a planted defect).
add_executable(bc_python152_exetest_wide ${PYHOST_DIR}/exetest.c)
target_link_libraries(bc_python152_exetest_wide PRIVATE bc_python152_wide)
target_compile_options(bc_python152_exetest_wide PRIVATE ${PY152_C_FLAGS})
target_compile_definitions(bc_python152_exetest_wide PRIVATE "BC_PY_EXETEST_DEFECT=\"\"")
add_test(NAME python152_exe_ints_wide_long COMMAND bc_python152_exetest_wide ${CMAKE_BINARY_DIR}/pytest_game_ints_wide
         ${CMAKE_CURRENT_LIST_DIR}/../tests/py152/ints.py)
set_tests_properties(python152_exe_ints_wide_long PROPERTIES WILL_FAIL TRUE)
