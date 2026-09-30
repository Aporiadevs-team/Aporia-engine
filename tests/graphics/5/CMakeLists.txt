# Render test 5 — HDR + reflections, dynamic orbit around the mirror axis.
# Headless: no window, renders offscreen and verifies pixels via readback.
cmake_minimum_required(VERSION 3.25)
project(AporiaRenderTestHdrDynamic LANGUAGES CXX)

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

add_executable(AporiaRenderTestHdrDynamic main.cpp)
target_link_libraries(AporiaRenderTestHdrDynamic PRIVATE Aporia::Engine)
set_target_properties(AporiaRenderTestHdrDynamic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
aporia_stage_engine_runtime(AporiaRenderTestHdrDynamic)
