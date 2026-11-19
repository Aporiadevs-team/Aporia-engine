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
option(APORIA_ENABLE_VULKAN "Vulkan backend" OFF)
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

if(APORIA_ENABLE_VULKAN AND NOT APORIA_ENABLE_GRAPHICS)
    message(FATAL_ERROR "APORIA_ENABLE_VULKAN requires APORIA_ENABLE_GRAPHICS")
endif()
if(APORIA_ENABLE_GRAPHICS AND NOT APORIA_ENABLE_VULKAN)
    message(FATAL_ERROR "The current graphics module requires APORIA_ENABLE_VULKAN")
endif()
