# Render test 4 — HDR + planar reflections, static scene.
# Headless: no window, renders offscreen and verifies pixels via readback.
cmake_minimum_required(VERSION 3.25)
project(AporiaRenderTestHdrReflections LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Standalone configuration pulls in the engine; from the engine root the
# AporiaEngine target already exists, so this is skipped (no recursion).
if(NOT TARGET AporiaEngine)
    set(APORIA_ENABLE_GRAPHICS ON CACHE BOOL "" FORCE)
    set(APORIA_ENABLE_VULKAN ON CACHE BOOL "" FORCE)
    set(APORIA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(APORIA_BUILD_HOST_SAMPLE OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../.. AporiaEngine)
endif()

add_executable(AporiaRenderTestHdrReflections main.cpp)
target_link_libraries(AporiaRenderTestHdrReflections PRIVATE Aporia::Engine)
set_target_properties(AporiaRenderTestHdrReflections PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
aporia_stage_engine_runtime(AporiaRenderTestHdrReflections)
