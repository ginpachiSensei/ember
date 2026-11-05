cmake_minimum_required(VERSION 3.25)

project(Ember VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(EmberCompileOptions)

option(EMBER_BUILD_TESTS "Build unit tests" ON)
option(EMBER_ENABLE_PROFILING "Build with Tracy profiler support" ON)
if(EMBER_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(external)

# Engine modules, lowest layer first. A module may only depend on modules above it in this list.
add_subdirectory(engine/core)
add_subdirectory(engine/platform)
add_subdirectory(engine/rhi)
add_subdirectory(engine/debugui)

# Applications
add_subdirectory(apps/sandbox)

set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ember_sandbox)
