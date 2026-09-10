# ppmi - the MUSIC <-> PeakPatch orchestrator.
#
# Deliberately dependency-free: a C++17 compiler and nothing else. The binary
# has to drop onto a bare cluster login node without an environment to install,
# which is the whole reason the orchestration layer is not a Python wrapper.
#
# MPI is optional and only used by the field tools, which stream tiles; the
# validation, config-generation and catalogue paths never need it.

cmake_minimum_required(VERSION 3.16)
project(ppmi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(ppmi_core
  src/geometry.cpp
  src/catalog.cpp
  src/field.cpp
  src/spec.cpp
  src/config.cpp
  src/feasibility.cpp
  src/manifest.cpp
)
target_include_directories(ppmi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(ppmi tools/ppmi_main.cpp)
target_link_libraries(ppmi PRIVATE ppmi_core)

enable_testing()
foreach(t geometry catalog field spec feasibility manifest)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE ppmi_core)
  target_include_directories(test_${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
