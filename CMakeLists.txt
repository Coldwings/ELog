cmake_minimum_required(VERSION 3.14)
project(elog CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
endif()

option(ELOG_LTO "Enable link-time optimization" OFF)
if(ELOG_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _elog_ipo_ok)
    if(_elog_ipo_ok)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    endif()
endif()

add_library(elog STATIC
    src/scratch.cpp
    src/level.cpp
    src/render.cpp
    src/render_extra.cpp
    src/grisu2.cpp
    src/sink.cpp
    src/buffered_file_sink.cpp
    src/rotating_sink.cpp
    src/prologue.cpp
    src/logger.cpp
    src/default_logger.cpp
    src/registry.cpp
    src/aggregate.cpp
)

target_include_directories(elog PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(elog PRIVATE -Wall -Wextra -Wpedantic)
target_compile_features(elog PUBLIC cxx_std_14)

find_package(Threads REQUIRED)
target_link_libraries(elog PUBLIC Threads::Threads)

option(ELOG_BUILD_TESTS "Build elog tests" ON)
if(ELOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

option(ELOG_BUILD_EXAMPLES "Build elog examples" ON)
if(ELOG_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

option(ELOG_BUILD_BENCHMARKS "Build elog benchmarks" ON)
if(ELOG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
