cmake_minimum_required(VERSION 3.24)
project(d2k_faithful VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(D2K_BUILD_TESTS "Build unit tests" ON)

# One self-contained exe (CLAUDE.md "zero-friction runtime"): the MSVC runtime
# is linked statically, so a clean Windows needs no Visual C++ redistributable.
# Set before the dependencies so SDL uses the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    # A Linux build that runs on older distributions: the C++ runtime is
    # linked in, so only glibc (2.35+, Ubuntu 22.04's) and system libraries remain.
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

include(cmake/Dependencies.cmake)

# Float arithmetic (CLAUDE.md section 10, docs/re/02): IEEE float32 on SSE2
# with every operation rounded as written, no FMA contraction, no fast-math,
# so every PC and both compilers compute the same game (lockstep). Only our
# targets: set after the dependencies (SDL does not build with /fp:strict).
if(MSVC)
    add_compile_options(/fp:strict)
else()
    add_compile_options(-ffp-contract=off -fno-fast-math)
endif()

add_subdirectory(src)
add_subdirectory(tools/d2k-tool)
include(cmake/Packaging.cmake)

if(D2K_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
