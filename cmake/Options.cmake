# Build options and language settings.

if(NOT WIN32)
    message(FATAL_ERROR "Aporia Engine supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Things to build.
option(APORIA_BUILD_TESTS "Build the CLI/graphics/text test runners" ${PROJECT_IS_TOP_LEVEL})
option(APORIA_BUILD_HOST_SAMPLE "Build the external host samples" ${PROJECT_IS_TOP_LEVEL})
option(APORIA_BUILD_RENDER_TESTS "Build tests/graphics/N render test projects" OFF)
option(APORIA_BUILD_VISIBLE_RENDER_TESTS "Build the interactive windowed render test" OFF)

# Optional modules.
option(APORIA_ENABLE_PHYSICS "Physics module" ON)
option(APORIA_ENABLE_INPUT "Input module" ON)
option(APORIA_INPUT_SDL3 "SDL3 input source (needs SDL3::SDL3)" OFF)
option(APORIA_INPUT_GLFW "GLFW input source (needs glfw)" OFF)
option(APORIA_ENABLE_GRAPHICS "Graphics module" OFF)
option(APORIA_ENABLE_D3D12 "Direct3D 12 backend" ON)
option(APORIA_ENABLE_D3D11 "Direct3D 11 backend" OFF)
option(APORIA_ENABLE_D3D9 "Direct3D 9 backend" OFF)
# Backend the renderer drives. "auto" picks the best available one at runtime.
# This branch is the DirectX port, so Direct3D 12 is the default; the
# APORIA_BACKEND environment variable overrides it at runtime.
set(APORIA_BACKEND "d3d12" CACHE STRING "Graphics backend: auto, d3d12, d3d11, d3d9")
set_property(CACHE APORIA_BACKEND PROPERTY STRINGS auto d3d12 d3d11 d3d9)
option(APORIA_ENABLE_VEHICLE "Vehicle module" OFF)
option(APORIA_ENABLE_SOFTBODY "Soft body module" OFF)
option(APORIA_ENABLE_AUDIO "Audio module" OFF)
option(APORIA_ENABLE_SCRIPTING "Scripting module" OFF)
option(APORIA_ENABLE_NETWORKING "Networking module" OFF)
# Unstable fast-path: new features live here first, then graduate to the stable
# path. Currently: multi-layer per-item materials, multithreaded frame
# recording, UI layers, and froxel volumetric fog.
option(APORIA_EXPERIMENTAL "Experimental features (unstable)" OFF)
# Seasons, weather-driven sky, precipitation and analytic fog. OFF keeps the
# weather API compiled but inert and the sky palette unchanged.
option(APORIA_SEASONS "Seasons and weather-driven sky" OFF)

if(APORIA_ENABLE_GRAPHICS AND NOT (APORIA_ENABLE_D3D12 OR APORIA_ENABLE_D3D11 OR APORIA_ENABLE_D3D9))
    message(FATAL_ERROR "The graphics module needs at least one Direct3D backend")
endif()
