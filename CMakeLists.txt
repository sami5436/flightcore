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

find_package(Threads REQUIRED)

# OS abstraction layer: the only code that calls pthreads directly.
add_library(fc_osal
  src/osal/osal.cpp)
add_library(fc::osal ALIAS fc_osal)
target_include_directories(fc_osal PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(fc_osal PUBLIC Threads::Threads)
fc_set_warnings(fc_osal)

# Core library. Every milestone adds sources here.
add_library(fc_core
  src/core/version.cpp
  src/core/scoped_timer.cpp
  src/core/thread.cpp)
add_library(fc::core ALIAS fc_core)
target_include_directories(fc_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_definitions(fc_core PUBLIC
  FC_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(fc_core PUBLIC fc::osal)
fc_set_warnings(fc_core)

if(FC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
