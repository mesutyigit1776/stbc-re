cmake_minimum_required(VERSION 3.20)
project(bc_source C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Results must follow the instruction order of the exe: no fused multiply-add, no fast-math.
add_compile_options(-ffp-contract=off -fno-fast-math -Wall -Wextra -Wno-invalid-offsetof)

# Precision of the x87 arithmetic that the engine reproduces: 53 (control word 0x027F, set by the C run time)
# or 24 (control word 0x007F, probably in effect after Direct3D starts, E0038).
# The default changes to 24 when E0038 is confirmed by observation (E0042 tells what counts as observation).
set(BC_X87_PRECISION 53 CACHE STRING "x87 precision control of the engine arithmetic: 53 or 24")
set_property(CACHE BC_X87_PRECISION PROPERTY STRINGS 53 24)
if(NOT BC_X87_PRECISION STREQUAL "53" AND NOT BC_X87_PRECISION STREQUAL "24")
  message(FATAL_ERROR "BC_X87_PRECISION must be 53 or 24")
endif()

file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/engine/*.cpp)
file(GLOB EMUTEST_SOURCES CONFIGURE_DEPENDS src/emutest/*.cpp)

# The engine and the emulator test library exist once per precision, so that one build tests both.
foreach(P 53 24)
  add_library(bc_engine_p${P} STATIC ${ENGINE_SOURCES})
  target_include_directories(bc_engine_p${P} PUBLIC src/engine)
  target_compile_definitions(bc_engine_p${P} PUBLIC BC_X87_PRECISION=${P})

  add_library(bc_emutest_p${P} SHARED ${EMUTEST_SOURCES})
  target_link_libraries(bc_emutest_p${P} PRIVATE bc_engine_p${P})
endforeach()

add_library(bc_engine ALIAS bc_engine_p${BC_X87_PRECISION})

include(third_party/python152.cmake)

# The SWIG runtime of the Appc module.
add_library(bc_swig STATIC src/pyhost/swig/swig_runtime.cpp)
target_include_directories(bc_swig PUBLIC src/pyhost/swig)
target_link_libraries(bc_swig PUBLIC bc_python152)

add_executable(bc_swig_test src/pyhost/swigtest.cpp)
target_link_libraries(bc_swig_test PRIVATE bc_swig)
add_test(NAME appc_swig_runtime COMMAND bc_swig_test)

# The Appc module: the generated wrappers and tables, the module init code and the start-up code.
include(tools/appcgen/appcgen.cmake)
set(BC_APPC_RUNTIME_SOURCES
  src/pyhost/appc/appc_module.cpp
  src/pyhost/startup.cpp
  src/pyhost/script_calls.cpp
  src/pyhost/host_tools.cpp)
add_library(bc_appc STATIC ${BC_APPC_RUNTIME_SOURCES})
appcgen_add_sources(bc_appc)
target_include_directories(bc_appc PUBLIC src/pyhost src/pyhost/appc)
target_link_libraries(bc_appc PUBLIC bc_swig bc_engine)

add_executable(bc_pyhost src/pyhost/bc_pyhost.cpp)
target_link_libraries(bc_pyhost PRIVATE bc_appc)

add_executable(bc_appc_test src/pyhost/appctest.cpp)
target_link_libraries(bc_appc_test PRIVATE bc_appc)

# Variants with a planted defect in the module tables. The module tests must fail on them.
foreach(DEFECT wrong_constant missing_method)
  add_library(bc_appc_${DEFECT} STATIC ${BC_APPC_RUNTIME_SOURCES})
  appcgen_add_sources(bc_appc_${DEFECT} OUT_DIR ${CMAKE_BINARY_DIR}/appc_${DEFECT} DEFECT ${DEFECT})
  target_include_directories(bc_appc_${DEFECT} PUBLIC src/pyhost src/pyhost/appc)
  target_link_libraries(bc_appc_${DEFECT} PUBLIC bc_swig bc_engine)
  add_executable(bc_appc_test_${DEFECT} src/pyhost/appctest.cpp)
  target_link_libraries(bc_appc_test_${DEFECT} PRIVATE bc_appc_${DEFECT})
endforeach()

# Game roots with a mod over the game folder. The mod folders hold only changed files.
set(BC_LAYERED_ROOTS)
foreach(MOD game remastered_1.2 orion_delta_rc4)
  set(ROOT_DIR ${CMAKE_BINARY_DIR}/layered_${MOD})
  set(OVERLAY_DIR ${CMAKE_SOURCE_DIR}/mods/${MOD})
  if(MOD STREQUAL "game")
    set(OVERLAY_DIR ${CMAKE_SOURCE_DIR}/game)
  endif()
  # The copies keep the file times of the sources, so a stamp file tells when the root was made.
  add_custom_command(OUTPUT ${ROOT_DIR}.stamp
    COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/make_layered_root.py
            ${CMAKE_SOURCE_DIR}/game ${OVERLAY_DIR} ${ROOT_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${ROOT_DIR}.stamp
    DEPENDS ${CMAKE_SOURCE_DIR}/tools/make_layered_root.py
    COMMENT "Making the game root with ${MOD}")
  list(APPEND BC_LAYERED_ROOTS ${ROOT_DIR}.stamp)
endforeach()
add_custom_target(layered_roots ALL DEPENDS ${BC_LAYERED_ROOTS})

set(APPC_TESTS ${CMAKE_SOURCE_DIR}/tests/appc)
set(APPC_TARGET ${CMAKE_SOURCE_DIR}/target)
add_test(NAME appc_constants COMMAND bc_appc_test ${APPC_TESTS}/constants.py ${APPC_TARGET}/appc_constants.tsv)
add_test(NAME appc_methods COMMAND bc_appc_test ${APPC_TESTS}/methods.py ${APPC_TARGET}/appc_method_table.tsv)
add_test(NAME appc_traps COMMAND bc_appc_test ${APPC_TESTS}/traps.py)
add_test(NAME appc_wrappers COMMAND bc_appc_test ${APPC_TESTS}/wrappers.py)
add_test(NAME appc_pointers COMMAND bc_appc_test ${APPC_TESTS}/pointers.py)

# A planted defect must make the check fail. Each test passes only when the expected check fails.
add_test(NAME appc_defect_wrong_constant
  COMMAND bc_appc_test_wrong_constant ${APPC_TESTS}/constants.py ${APPC_TARGET}/appc_constants.tsv)
set_tests_properties(appc_defect_wrong_constant PROPERTIES
  PASS_REGULAR_EXPRESSION "FAIL constants: all values equal to the table")
add_test(NAME appc_defect_missing_method
  COMMAND bc_appc_test_missing_method ${APPC_TESTS}/methods.py ${APPC_TARGET}/appc_method_table.tsv)
set_tests_properties(appc_defect_missing_method PROPERTIES
  PASS_REGULAR_EXPRESSION "FAIL methods: every name is an attribute of Appc")

# import App with each folder as the game root (App only: the game and mod folders stay unchanged).
add_test(NAME appc_import_game COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/game --app-only)
add_test(NAME appc_import_remastered COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/mods/remastered_1.2 --app-only)
add_test(NAME appc_import_orion COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/mods/orion_delta_rc4 --app-only)
set_tests_properties(appc_import_game appc_import_remastered appc_import_orion PROPERTIES
  PASS_REGULAR_EXPRESSION "import App: ok")

# The whole Python start-up in the layered roots, which may be written to.
add_test(NAME appc_startup_game COMMAND bc_pyhost ${CMAKE_BINARY_DIR}/layered_game)
add_test(NAME appc_startup_remastered COMMAND bc_pyhost ${CMAKE_BINARY_DIR}/layered_remastered_1.2)
add_test(NAME appc_startup_orion COMMAND bc_pyhost ${CMAKE_BINARY_DIR}/layered_orion_delta_rc4)
set_tests_properties(appc_startup_game appc_startup_remastered appc_startup_orion PROPERTIES
  PASS_REGULAR_EXPRESSION "FixApp.FixApp: ok")

# The icon classes as a script of the game uses them (the real script ShipIcons loads a group). The host takes the
# texture sizes from the TGA files of the game folder.
add_test(NAME pyhost_icons COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/game --run ${CMAKE_SOURCE_DIR}/tests/pyhost/icons.py)
set_tests_properties(pyhost_icons PROPERTIES PASS_REGULAR_EXPRESSION "icons: all checks passed")
# A host that gives no texture for any file must make the checks of the sizes fail.
add_test(NAME pyhost_icons_defect COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/game --run ${CMAKE_SOURCE_DIR}/tests/pyhost/icons.py)
set_tests_properties(pyhost_icons_defect PROPERTIES ENVIRONMENT BC_PYHOST_DEFECT=no_textures
  PASS_REGULAR_EXPRESSION "FAIL the first icon has the size of its file")

# The font manager as the script Icons.FontsAndIcons uses it: the real script of each tree registers its fonts and icon
# groups. The loader modules are replaced by modules that note their call.
add_test(NAME pyhost_fonts_game COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/game --run ${CMAKE_SOURCE_DIR}/tests/pyhost/fonts.py)
add_test(NAME pyhost_fonts_remastered COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/mods/remastered_1.2 --run ${CMAKE_SOURCE_DIR}/tests/pyhost/fonts.py)
add_test(NAME pyhost_fonts_orion COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/mods/orion_delta_rc4 --run ${CMAKE_SOURCE_DIR}/tests/pyhost/fonts.py)
set_tests_properties(pyhost_fonts_game pyhost_fonts_remastered pyhost_fonts_orion PROPERTIES
  PASS_REGULAR_EXPRESSION "fonts: all checks passed")
# A host that swaps the module and the function of a script call must make the checks fail.
add_test(NAME pyhost_fonts_defect COMMAND bc_pyhost ${CMAKE_SOURCE_DIR}/game --run ${CMAKE_SOURCE_DIR}/tests/pyhost/fonts.py)
set_tests_properties(pyhost_fonts_defect PROPERTIES ENVIRONMENT BC_PYHOST_DEFECT=swap_script_texts
  PASS_REGULAR_EXPRESSION "FAIL the registered font runs its loader through the icon manager")

# The bytecode oracle on a fixed sample of four files. The second test plants a one-byte change in the first
# shipped file and passes only when the oracle reports the difference.
set(ORACLE_COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/bytecode_oracle.py
  --host $<TARGET_FILE:bc_pyhost> --files ${CMAKE_SOURCE_DIR}/tests/oracle/sample.txt)
add_test(NAME oracle_sample COMMAND ${ORACLE_COMMAND})
set_tests_properties(oracle_sample PROPERTIES PASS_REGULAR_EXPRESSION "remastered: files compared 4, equal 4.*PASS oracle")
add_test(NAME oracle_sample_defect COMMAND ${ORACLE_COMMAND} --defect)
set_tests_properties(oracle_sample_defect PROPERTIES
  PASS_REGULAR_EXPRESSION "remastered: files compared 4, equal 3, different 1.*class code bytes.*FAIL oracle")

# The oracle ignores the number of exponent digits of a float text and nothing else. The second test breaks the rule.
add_test(NAME oracle_spelling COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tests/oracle/spelling_check.py)
set_tests_properties(oracle_spelling PROPERTIES PASS_REGULAR_EXPRESSION "PASS oracle spelling")
add_test(NAME oracle_spelling_defect COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tests/oracle/spelling_check.py --defect)
set_tests_properties(oracle_spelling_defect PROPERTIES PASS_REGULAR_EXPRESSION "FAIL oracle spelling")

# The gate gives the result engine to a stop at engine code that is not reconstructed, and crash to a real crash. The second
# test uses the old rule and must fail.
add_test(NAME gate_classes COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tests/gate/classify_check.py)
set_tests_properties(gate_classes PROPERTIES PASS_REGULAR_EXPRESSION "PASS gate classes")
add_test(NAME gate_classes_defect COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tests/gate/classify_check.py --defect)
set_tests_properties(gate_classes_defect PROPERTIES PASS_REGULAR_EXPRESSION "FAIL gate classes")

# The import gate on three modules with known results (stock game folder). The second test runs the same
# modules with traps that return None; the known results then change and the check must fail.
foreach(VARIANT ok defect)
  set(GATE_COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/import_gate.py
    --host $<TARGET_FILE:bc_pyhost> --work ${CMAKE_BINARY_DIR}/gate_sample_${VARIANT} --out ${CMAKE_BINARY_DIR}/gate_sample_${VARIANT}
    --source stock --module AI --module MakePYC --module loadsplash
    --expect ${CMAKE_SOURCE_DIR}/tests/gate/sample_expected.tsv)
  if(VARIANT STREQUAL "defect")
    add_test(NAME gate_sample_defect COMMAND ${GATE_COMMAND} --defect)
    set_tests_properties(gate_sample_defect PROPERTIES PASS_REGULAR_EXPRESSION "FAIL gate sample: known results")
  else()
    add_test(NAME gate_sample COMMAND ${GATE_COMMAND})
    set_tests_properties(gate_sample PROPERTIES PASS_REGULAR_EXPRESSION "PASS gate sample: known results")
  endif()
endforeach()
