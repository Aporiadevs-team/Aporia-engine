cmake_minimum_required(VERSION 3.25)
project(AporiaEngine VERSION 1.0.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Options)   # build options + language settings
include(Sources)   # APORIA_SOURCES (globbed per module)
include(Runtime)   # aporia_stage_engine_runtime() + host executable helper

# ---------------------------------------------------------------------------
# AporiaEngine shared library
# ---------------------------------------------------------------------------
add_library(AporiaEngine SHARED ${APORIA_SOURCES})
add_library(Aporia::Engine ALIAS AporiaEngine)

target_compile_features(AporiaEngine PUBLIC cxx_std_20)
target_include_directories(AporiaEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/stb")
target_compile_definitions(AporiaEngine PRIVATE APORIA_BUILDING_LIBRARY)
target_compile_definitions(AporiaEngine INTERFACE APORIA_USING_LIBRARY)

# stb_truetype emits a wall of warnings; silence it for that single file.
set_source_files_properties(src/Graphics/Text/FontAtlasBuilder.cpp
    PROPERTIES COMPILE_OPTIONS "/w")

if(MSVC)
    target_compile_options(AporiaEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(AporiaEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(AporiaEngine PROPERTIES
    OUTPUT_NAME "AporiaEngine"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
if(WIN32)
    set_target_properties(AporiaEngine PROPERTIES PREFIX "")
endif()

# ---------------------------------------------------------------------------
# Optional modules
# ---------------------------------------------------------------------------
if(APORIA_ENABLE_PHYSICS)
    target_compile_definitions(AporiaEngine PUBLIC APORIA_ENGINE_HAS_PHYSICS)
endif()

if(APORIA_ENABLE_INPUT)
    target_compile_definitions(AporiaEngine PUBLIC APORIA_ENGINE_HAS_INPUT)
    if(APORIA_INPUT_SDL3 AND TARGET SDL3::SDL3)
        target_link_libraries(AporiaEngine PRIVATE SDL3::SDL3)
    endif()
    if(APORIA_INPUT_GLFW AND TARGET glfw)
        target_link_libraries(AporiaEngine PRIVATE glfw)
    endif()
endif()

if(APORIA_ENABLE_GRAPHICS)
    # The Direct3D renderer compiles its HLSL at runtime, so the source is
    # embedded into the library as a string constant.
    include(Hlsl)
    target_include_directories(AporiaEngine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(AporiaEngine PUBLIC APORIA_ENGINE_HAS_GRAPHICS)
    target_compile_definitions(AporiaEngine PRIVATE
        "APORIA_BACKEND_DEFAULT=\"${APORIA_BACKEND}\"")
    if(APORIA_EXPERIMENTAL)
        target_compile_definitions(AporiaEngine PUBLIC APORIA_EXPERIMENTAL=1)
    endif()
    if(APORIA_SEASONS)
        target_compile_definitions(AporiaEngine PUBLIC APORIA_SEASONS=1)
    endif()
    if(WIN32)
        target_link_libraries(AporiaEngine PRIVATE user32)
    endif()
endif()

# ---------------------------------------------------------------------------
# Direct3D backends
# ---------------------------------------------------------------------------
if(APORIA_ENABLE_D3D12)
    target_compile_definitions(AporiaEngine PRIVATE APORIA_ENABLE_D3D12=1)
    target_link_libraries(AporiaEngine PRIVATE d3d12 dxgi d3dcompiler)
endif()

# ---------------------------------------------------------------------------
# Install rules for downstream projects (find_package(AporiaEngine))
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS AporiaEngine
    EXPORT AporiaEngineTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/Aporia
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp")

install(EXPORT AporiaEngineTargets
    NAMESPACE Aporia::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AporiaEngine)

# ---------------------------------------------------------------------------
# Tests, render tests and host samples
# ---------------------------------------------------------------------------
include(Tests)
