cmake_minimum_required(VERSION 3.28)
# Host tools are separate from the target triplet; this milestone supports x86-64 hosts.
if(NOT VCPKG_HOST_TRIPLET)
  if(CMAKE_HOST_WIN32)
    set(VCPKG_HOST_TRIPLET x64-windows CACHE STRING "vcpkg host tools triplet")
  else()
    set(VCPKG_HOST_TRIPLET x64-linux CACHE STRING "vcpkg host tools triplet")
  endif()
endif()
project(renderengine VERSION 0.1.0 LANGUAGES C CXX)

find_package(volk CONFIG REQUIRED)
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
find_program(SLANGC slangc HINTS "${SLANG_ROOT}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(SPIRV_VAL spirv-val
  PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}/tools/spirv-tools"
  NO_DEFAULT_PATH REQUIRED)
execute_process(COMMAND "${SLANGC}" -version OUTPUT_VARIABLE slang_version
  ERROR_VARIABLE slang_version_error RESULT_VARIABLE slang_result)
if(NOT slang_result EQUAL 0 OR NOT "${slang_version}${slang_version_error}" MATCHES "^2026\\.17([\r\n]|$)")
  message(FATAL_ERROR "Slang 2026.17 is required; run scripts/bootstrap.py")
endif()

set(shader_headers)
foreach(stage vertex fragment)
  set(binary "${CMAKE_CURRENT_BINARY_DIR}/${stage}.spv")
  set(header "${CMAKE_CURRENT_BINARY_DIR}/generated/${stage}_spv.hpp")
  add_custom_command(OUTPUT "${header}"
    COMMAND "${SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/fixture.slang"
      -entry "${stage}Main" -stage ${stage} -target spirv -profile spirv_1_5
      -matrix-layout-row-major -emit-spirv-directly -o "${binary}"
    COMMAND "${SPIRV_VAL}" --target-env vulkan1.2 "${binary}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_spirv.py"
      "${binary}" "${header}" "${stage}_spv"
    DEPENDS shaders/fixture.slang scripts/embed_spirv.py
    BYPRODUCTS "${binary}" VERBATIM)
  list(APPEND shader_headers "${header}")
endforeach()

add_library(renderengine_core STATIC src/renderer.cpp ${shader_headers})
add_library(renderengine::renderengine ALIAS renderengine_core)
target_sources(renderengine_core PUBLIC FILE_SET CXX_MODULES FILES src/renderengine.cppm)
target_compile_features(renderengine_core PUBLIC cxx_std_20)
target_include_directories(renderengine_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(renderengine_core PRIVATE volk::volk)

add_executable(renderengine src/main.cpp)
target_link_libraries(renderengine PRIVATE renderengine::renderengine)
foreach(target renderengine_core renderengine)
  set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()

include(CTest)
set(RENDERENGINE_TEST_DEVICE "any" CACHE STRING "Test device class: any, hardware, software")
option(RENDERENGINE_TEST_VALIDATION "Require Khronos synchronization validation in tests" OFF)
if(BUILD_TESTING)
  add_subdirectory(tests/consumer)
  set(validation_arg)
  if(RENDERENGINE_TEST_VALIDATION)
    set(validation_arg --validation)
  endif()
  add_test(NAME oracle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_oracle.py")
  add_test(NAME harness COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_acceptance.py")
  add_test(NAME scene_oracle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scenes.py")
  add_test(NAME transform_oracle COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_transforms.py")
  add_test(NAME scene_input COMMAND scene_consumer --invalid-only)
  add_test(NAME consumer_build COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/consumer_build.py"
    "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties(consumer_build PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)
  set_tests_properties(scene_input PROPERTIES
    ENVIRONMENT "VK_DRIVER_FILES=renderengine-no-such-icd.json" TIMEOUT 30)
  add_test(NAME scenes COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/scenes.py"
    $<TARGET_FILE:scene_consumer> "${CMAKE_CURRENT_BINARY_DIR}/images/scenes"
    --device "${RENDERENGINE_TEST_DEVICE}" ${validation_arg})
  set_tests_properties(scenes PROPERTIES TIMEOUT 150)
  add_test(NAME render COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/acceptance.py"
    $<TARGET_FILE:renderengine> "${CMAKE_CURRENT_BINARY_DIR}/images"
    --device "${RENDERENGINE_TEST_DEVICE}" ${validation_arg})
  set_tests_properties(render PROPERTIES TIMEOUT 180)
endif()
