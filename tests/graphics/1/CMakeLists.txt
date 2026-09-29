# Render test 1 — 2D: dynamic body moved by keyboard input, ortho camera.
# Standalone project: SDL3 owns the window, Aporia owns render/physics/input.
#
# Build standalone:
#   cmake -S tests/graphics/1 -B build-render-test-1
#   cmake --build build-render-test-1 --target AporiaRenderTest2D
# Or from the engine root with -DAPORIA_BUILD_RENDER_TESTS=ON.
cmake_minimum_required(VERSION 3.25)
project(AporiaRenderTest2D LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Standalone configuration pulls in the engine; from the engine root the
# AporiaEngine target already exists, so this is skipped (no recursion).
if(NOT TARGET AporiaEngine)
    set(APORIA_ENABLE_PHYSICS ON CACHE BOOL "" FORCE)
    set(APORIA_ENABLE_INPUT ON CACHE BOOL "" FORCE)
    set(APORIA_ENABLE_GRAPHICS ON CACHE BOOL "" FORCE)
    set(APORIA_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
    set(APORIA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(APORIA_BUILD_HOST_SAMPLE OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. AporiaEngine)
endif()

if(NOT APORIA_ENABLE_PHYSICS OR NOT APORIA_ENABLE_INPUT OR NOT APORIA_ENABLE_GRAPHICS)
    message(FATAL_ERROR "Render tests require the physics, input and graphics modules")
endif()

include(FetchContent)
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
# MinGW (GCC 16) cannot compile SDL's bundled libm (uclibc AVX intrinsics);
# force the system C library instead.
set(SDL_LIBC OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.2.30
)
FetchContent_MakeAvailable(SDL3)

if(TARGET SDL3::SDL3)
    set(APORIA_SDL3_LIB SDL3::SDL3)
else()
    set(APORIA_SDL3_LIB SDL3::SDL3-shared)
endif()

add_executable(AporiaRenderTest2D main.cpp)
target_link_libraries(AporiaRenderTest2D PRIVATE Aporia::Engine ${APORIA_SDL3_LIB})
set_target_properties(AporiaRenderTest2D PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
aporia_stage_engine_runtime(AporiaRenderTest2D)
add_custom_command(TARGET AporiaRenderTest2D POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:${APORIA_SDL3_LIB}>"
        "$<TARGET_FILE_DIR:AporiaRenderTest2D>"
)
