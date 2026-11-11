# Render test 12 — lighting lab. SDL3 owns the window; Aporia owns render and
# text. Colored point/spot lights orbit a block scene; their colour, intensity,
# soft falloff radius and softness can be tweaked live, and the quality preset
# caps how many lights reach the shader (Low 0 / Medium 8 / High 16).
#
# Build standalone:
#   cmake -S tests/graphics/12 -B build-render-test-12
#   cmake --build build-render-test-12 --target AporiaRenderTest12
# Or from the engine root with -DAPORIA_BUILD_RENDER_TESTS=ON.
cmake_minimum_required(VERSION 3.25)
project(AporiaRenderTest12 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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

add_executable(AporiaRenderTest12 main.cpp)
target_link_libraries(AporiaRenderTest12 PRIVATE Aporia::Engine ${APORIA_SDL3_LIB})
set_target_properties(AporiaRenderTest12 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
aporia_stage_engine_runtime(AporiaRenderTest12)
add_custom_command(TARGET AporiaRenderTest12 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:${APORIA_SDL3_LIB}>"
        "$<TARGET_FILE_DIR:AporiaRenderTest12>"
)
