# Ferment Family — standalone home of the Ferment plugin suite.
#
# Migrated from the R&D repo (history preserved via git filter-repo).
# No Airwindows code besides src/airwin_consolidated_base.* — the tiny
# MIT-licensed AudioEffect compatibility shim the pure-DSP layer builds on.
# That shim IS the frozen param ABI: index-based setParameter/getParameter,
# indices never reorder, new parameters append only (Cuts iOS vendors the
# src-ferment tree and downstream pipelines address parameters by index).

cmake_minimum_required(VERSION 3.21)
project(ferment-family VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if (APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")
endif()

option(COPY_PLUGIN_AFTER_BUILD "Install plugins to user plugin folders after build" TRUE)
option(FERMENT_BUILD_TESTS "Build the DSP test executables" ON)

execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE BUILD_HASH
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT BUILD_HASH)
  set(BUILD_HASH "unknown")
endif()

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME JUCE
  GITHUB_REPOSITORY juce-framework/JUCE
  GIT_TAG 8.0.4
)
CPMAddPackage(
  NAME clap-juce-extensions
  GITHUB_REPOSITORY free-audio/clap-juce-extensions
  GIT_TAG main
  SUBMODULE_RECURSIVE ON
)

# The AudioEffect shim the dual-layer DSP classes derive from.
add_library(ferment-base STATIC src/airwin_consolidated_base.cpp)
target_include_directories(ferment-base PUBLIC src)

add_subdirectory(src-ferment)

# ---- tests ------------------------------------------------------------------
# Pure-DSP tests build without JUCE; the processor-level tests need it.
# (tests/sidechain_test.cpp is NOT built here — it needs airwin-registry and
# stays runnable in the the R&D repo repo.)

if (FERMENT_BUILD_TESTS)
  enable_testing()

  add_executable(biquad_tests tests/biquad_test.cpp)
  target_compile_definitions(biquad_tests PRIVATE _USE_MATH_DEFINES)
  add_test(NAME biquad_tests COMMAND biquad_tests)

  function(ferment_add_dsp_test NAME MAIN)
    add_executable(${NAME} ${MAIN} ${ARGN} src/airwin_consolidated_base.cpp)
    target_compile_definitions(${NAME} PRIVATE _USE_MATH_DEFINES)
    add_test(NAME ${NAME} COMMAND ${NAME})
  endfunction()

  ferment_add_dsp_test(charge_tests tests/charge_test.cpp
      src-ferment/charge/FermentCharge.cpp src-ferment/charge/FermentChargeProc.cpp)
  ferment_add_dsp_test(charge_edge_tests tests/charge_edge_test.cpp
      src-ferment/charge/FermentCharge.cpp src-ferment/charge/FermentChargeProc.cpp)
  ferment_add_dsp_test(clip_tests tests/clip_test.cpp
      src-ferment/clip/FermentClip.cpp src-ferment/clip/FermentClipProc.cpp)
  ferment_add_dsp_test(limit_tests tests/limit_test.cpp
      src-ferment/limit/FermentLimit.cpp src-ferment/limit/FermentLimitProc.cpp)

  function(ferment_add_processor_test NAME MAIN)
    add_executable(${NAME} ${MAIN} ${ARGN}
        src/airwin_consolidated_base.cpp)
    target_link_libraries(${NAME} PRIVATE
        ferment::ferment_ui
        juce::juce_audio_utils
        juce::juce_audio_processors)
    target_compile_definitions(${NAME} PRIVATE
        _USE_MATH_DEFINES=1
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_CAMERA=disabled)
    add_test(NAME ${NAME} COMMAND ${NAME})
  endfunction()

  ferment_add_processor_test(utility_tests tests/utility_test.cpp
      src-ferment/utility/FermentUtilityProcessor.cpp
      src-ferment/utility/FermentUtilityEditor.cpp
      src-ferment/utility/FermentUtility.cpp)
  ferment_add_processor_test(utility_equivalence_test tests/utility_equivalence_test.cpp
      src-ferment/utility/FermentUtilityProcessor.cpp
      src-ferment/utility/FermentUtilityEditor.cpp
      src-ferment/utility/FermentUtility.cpp)
  ferment_add_processor_test(eq_equivalence_test tests/eq_equivalence_test.cpp
      src-ferment/eq/FermentEqProcessor.cpp
      src-ferment/eq/FermentEqEditor.cpp
      src-ferment/eq/FermentEq.cpp)
  ferment_add_processor_test(glue_equivalence_test tests/glue_equivalence_test.cpp
      src-ferment/glue/FermentGlueProcessor.cpp
      src-ferment/glue/FermentGlueEditor.cpp
      src-ferment/glue/FermentGlue.cpp
      src-ferment/glue/FermentGlueProc.cpp)

  # UI-spec acceptance: the drawn EQ curve must match what the DSP does.
  ferment_add_processor_test(eq_response_test tests/eq_response_test.cpp
      src-ferment/eq/FermentEqProcessor.cpp
      src-ferment/eq/FermentEqEditor.cpp
      src-ferment/eq/FermentEq.cpp)
endif()
