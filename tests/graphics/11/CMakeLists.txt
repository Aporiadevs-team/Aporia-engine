# Render test 11 — multithreaded render lab. SDL3 owns the window; Aporia owns
# render, physics and text. Left pane: falling cubes (physics). Center: text
# rotating clockwise. Right: a WASD (or --auto) driven circle. Full per-section
# frame accounting is reported live and summarized on exit. Press M to toggle
# experimental multithreaded recording.
#
# Build standalone:
#   cmake -S tests/graphics/11 -B build-render-test-11
#   cmake --build build-render-test-11 --target AporiaRenderTest11
# Or from the engine root with -DAPORIA_BUILD_RENDER_TESTS=ON.
cmake_minimum_required(VERSION 3.25)
project(AporiaRenderTest11 LANGUAGES CXX)

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

add_executable(AporiaRenderTest11 main.cpp)
target_link_libraries(AporiaRenderTest11 PRIVATE Aporia::Engine ${APORIA_SDL3_LIB})
set_target_properties(AporiaRenderTest11 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
aporia_stage_engine_runtime(AporiaRenderTest11)
add_custom_command(TARGET AporiaRenderTest11 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:${APORIA_SDL3_LIB}>"
        "$<TARGET_FILE_DIR:AporiaRenderTest11>"
)
