cmake_minimum_required(VERSION 3.25)

project(flightcore
  VERSION 0.1.0
  DESCRIPTION "Mini real-time flight software framework"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FC_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Warnings)
include(Sanitizers)

# Core library. Every milestone adds sources here.
add_library(fc_core
  src/core/version.cpp)
add_library(fc::core ALIAS fc_core)
target_include_directories(fc_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_definitions(fc_core PUBLIC
  FC_VERSION_STRING="${PROJECT_VERSION}")
fc_set_warnings(fc_core)

if(FC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
