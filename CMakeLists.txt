cmake_minimum_required(VERSION 3.25)
project(AporiaEngine VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "No support for non-Windows systems. Aporia Engine currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(APORIA_BUILD_TESTS "Build the Aporia CLI test runner" ${PROJECT_IS_TOP_LEVEL})
option(APORIA_BUILD_HOST_SAMPLE "Build the external host sample" ${PROJECT_IS_TOP_LEVEL})
option(APORIA_BUILD_RENDER_TESTS "Build tests/graphics render test projects (1: 2D SDL3, 2: 3D SDL3)" OFF)
option(APORIA_BUILD_VISIBLE_RENDER_TESTS "Build an interactive windowed Vulkan render test" OFF)
option(APORIA_ENABLE_PHYSICS "Enable the physics module" ON)
option(APORIA_ENABLE_INPUT "Enable the input module" ON)
option(APORIA_ENABLE_GRAPHICS "Enable the graphics module" OFF)
option(APORIA_ENABLE_VULKAN "Enable the Vulkan backend" OFF)
option(APORIA_ENABLE_VEHICLE "Enable the vehicle module" OFF)
option(APORIA_ENABLE_SOFTBODY "Enable the soft body module" OFF)
option(APORIA_ENABLE_AUDIO "Enable the audio module" OFF)
option(APORIA_ENABLE_SCRIPTING "Enable the scripting module" OFF)
option(APORIA_ENABLE_NETWORKING "Enable the networking module" OFF)

if(APORIA_ENABLE_VULKAN AND NOT APORIA_ENABLE_GRAPHICS)
    message(FATAL_ERROR "APORIA_ENABLE_VULKAN requires APORIA_ENABLE_GRAPHICS")
endif()

if(APORIA_ENABLE_GRAPHICS AND NOT APORIA_ENABLE_VULKAN)
    message(FATAL_ERROR "The current graphics module requires APORIA_ENABLE_VULKAN")
endif()

set(APORIA_SOURCES
    src/Game.cpp
    src/Scene.cpp
)

if (APORIA_ENABLE_INPUT)
    list(APPEND APORIA_SOURCES src/Platform/WIN32/InputWin32.cpp)
endif()

if (APORIA_ENABLE_PHYSICS)
    list(APPEND APORIA_SOURCES
        src/Physics/World/PhysicsWorld.cpp
        src/Terrain/Terrain.cpp
        src/Terrain/Generation/City/City.cpp)
endif()

if(APORIA_ENABLE_GRAPHICS)
    list(APPEND APORIA_SOURCES
        src/Graphics/GlobalDrawer/MeshPrimitives.cpp
        src/Graphics/GlobalDrawer/Device/Device.cpp
        src/Graphics/GlobalDrawer/Meshes/MeshRenderer.cpp
        src/Graphics/GlobalDrawer/Rectangulars/RectRenderer.cpp
        src/Graphics/GlobalDrawer/Shadows/ShadowRenderer.cpp
        src/Graphics/GlobalDrawer/Hdr/HdrRenderer.cpp
        src/Graphics/TextureManager/TextureManager.cpp)
endif()

if(APORIA_ENABLE_GRAPHICS)
    find_package(Vulkan REQUIRED)
    list(APPEND APORIA_SOURCES src/Graphics/GlobalDrawer/Graphics.cpp)

    set(APORIA_VULKAN_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders")
    set(APORIA_VULKAN_VERTEX_SHADER "${APORIA_VULKAN_SHADER_DIR}/triangle.vert.spv")
    set(APORIA_VULKAN_FRAGMENT_SHADER "${APORIA_VULKAN_SHADER_DIR}/triangle.frag.spv")
    set(APORIA_VULKAN_MESH_VERTEX_SHADER "${APORIA_VULKAN_SHADER_DIR}/mesh.vert.spv")
    set(APORIA_VULKAN_MESH_FRAGMENT_SHADER "${APORIA_VULKAN_SHADER_DIR}/mesh.frag.spv")
    set(APORIA_VULKAN_SHADOW_VERTEX_SHADER "${APORIA_VULKAN_SHADER_DIR}/shadow.vert.spv")
    set(APORIA_VULKAN_SHADOW_FRAGMENT_SHADER "${APORIA_VULKAN_SHADER_DIR}/shadow.frag.spv")
    set(APORIA_VULKAN_TONEMAP_VERTEX_SHADER "${APORIA_VULKAN_SHADER_DIR}/tonemap.vert.spv")
    set(APORIA_VULKAN_TONEMAP_FRAGMENT_SHADER "${APORIA_VULKAN_SHADER_DIR}/tonemap.frag.spv")

    add_custom_command(
        OUTPUT "${APORIA_VULKAN_VERTEX_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert"
            -o "${APORIA_VULKAN_VERTEX_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.vert"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_FRAGMENT_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag"
            -o "${APORIA_VULKAN_FRAGMENT_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/triangle.frag"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_MESH_VERTEX_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert"
            -o "${APORIA_VULKAN_MESH_VERTEX_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_MESH_FRAGMENT_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.frag"
            -o "${APORIA_VULKAN_MESH_FRAGMENT_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.frag"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_SHADOW_VERTEX_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert"
            -o "${APORIA_VULKAN_SHADOW_VERTEX_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_SHADOW_FRAGMENT_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag"
            -o "${APORIA_VULKAN_SHADOW_FRAGMENT_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.frag"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_TONEMAP_VERTEX_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.vert"
            -o "${APORIA_VULKAN_TONEMAP_VERTEX_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.vert"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${APORIA_VULKAN_TONEMAP_FRAGMENT_SHADER}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.frag"
            -o "${APORIA_VULKAN_TONEMAP_FRAGMENT_SHADER}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/tonemap.frag"
        VERBATIM
    )
    add_custom_target(AporiaVulkanShaders DEPENDS
        "${APORIA_VULKAN_VERTEX_SHADER}"
        "${APORIA_VULKAN_FRAGMENT_SHADER}"
        "${APORIA_VULKAN_MESH_VERTEX_SHADER}"
        "${APORIA_VULKAN_MESH_FRAGMENT_SHADER}"
        "${APORIA_VULKAN_SHADOW_VERTEX_SHADER}"
        "${APORIA_VULKAN_SHADOW_FRAGMENT_SHADER}"
        "${APORIA_VULKAN_TONEMAP_VERTEX_SHADER}"
        "${APORIA_VULKAN_TONEMAP_FRAGMENT_SHADER}"
    )
endif()

add_library(AporiaEngine SHARED ${APORIA_SOURCES})
add_library(Aporia::Engine ALIAS AporiaEngine)

target_compile_features(AporiaEngine PUBLIC cxx_std_20)
target_include_directories(AporiaEngine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_definitions(AporiaEngine PRIVATE APORIA_BUILDING_LIBRARY)
target_compile_definitions(AporiaEngine INTERFACE APORIA_USING_LIBRARY)

if (APORIA_ENABLE_PHYSICS)
    target_compile_definitions(AporiaEngine PUBLIC APORIA_ENGINE_HAS_PHYSICS)
endif()

if (APORIA_ENABLE_INPUT)
    target_compile_definitions(AporiaEngine PUBLIC APORIA_ENGINE_HAS_INPUT)
endif()

if(APORIA_ENABLE_GRAPHICS)
    target_include_directories(AporiaEngine SYSTEM PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_compile_definitions(AporiaEngine PUBLIC APORIA_ENGINE_HAS_GRAPHICS)
    target_compile_definitions(AporiaEngine PRIVATE
        "APORIA_VULKAN_SHADER_DIR=\"${APORIA_VULKAN_SHADER_DIR}\""
    )
    target_link_libraries(AporiaEngine PRIVATE Vulkan::Vulkan)
    if(WIN32)
        target_link_libraries(AporiaEngine PRIVATE user32)
    endif()
    add_dependencies(AporiaEngine AporiaVulkanShaders)
endif()

set_target_properties(AporiaEngine PROPERTIES
    OUTPUT_NAME "AporiaEngine"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if (WIN32)
    set_target_properties(AporiaEngine PROPERTIES PREFIX "")
endif()

if (MSVC)
    target_compile_options(AporiaEngine PRIVATE /W4 /permissive-)
else()
    target_compile_options(AporiaEngine PRIVATE -Wall -Wextra -Wpedantic)
endif()

function(aporia_stage_engine_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:AporiaEngine>"
            "$<TARGET_FILE_DIR:${target}>"
    )
endfunction()

if (APORIA_BUILD_RENDER_TESTS)
    if(NOT APORIA_ENABLE_GRAPHICS)
        message(FATAL_ERROR "APORIA_BUILD_RENDER_TESTS requires APORIA_ENABLE_GRAPHICS and APORIA_ENABLE_VULKAN")
    endif()
    add_subdirectory(tests/graphics/1 aporia-render-test-1)
    add_subdirectory(tests/graphics/2 aporia-render-test-2)
    add_subdirectory(tests/graphics/3 aporia-render-test-3)
    add_subdirectory(tests/graphics/4 aporia-render-test-4)
    add_subdirectory(tests/graphics/5 aporia-render-test-5)
    add_subdirectory(tests/graphics/6 aporia-render-test-6)
    add_subdirectory(tests/graphics/7 aporia-render-test-7)
endif()

if(APORIA_BUILD_VISIBLE_RENDER_TESTS)
    if(NOT WIN32 OR NOT APORIA_ENABLE_GRAPHICS)
        message(FATAL_ERROR "APORIA_BUILD_VISIBLE_RENDER_TESTS requires Windows and APORIA_ENABLE_GRAPHICS")
    endif()
    add_executable(AporiaRenderTestVisible tests/graphics/visible_main.cpp)
    target_link_libraries(AporiaRenderTestVisible PRIVATE Aporia::Engine user32)
    set_target_properties(AporiaRenderTestVisible PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    aporia_stage_engine_runtime(AporiaRenderTestVisible)
endif()

if (APORIA_BUILD_TESTS)
    enable_testing()
    add_executable(AporiaCli tests/cli/main.cpp)
    target_link_libraries(AporiaCli PRIVATE Aporia::Engine)
    set_target_properties(AporiaCli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    aporia_stage_engine_runtime(AporiaCli)
    add_test(NAME AporiaCli COMMAND AporiaCli)

    if(APORIA_ENABLE_GRAPHICS)
        add_executable(AporiaGraphicsCli tests/graphics/main.cpp)
        target_link_libraries(AporiaGraphicsCli PRIVATE Aporia::Engine)
        set_target_properties(AporiaGraphicsCli PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        aporia_stage_engine_runtime(AporiaGraphicsCli)
        add_test(NAME AporiaGraphicsCli COMMAND AporiaGraphicsCli)

        add_executable(AporiaGraphicsStress tests/graphics/main_stress.cpp)
        target_link_libraries(AporiaGraphicsStress PRIVATE Aporia::Engine psapi)
        set_target_properties(AporiaGraphicsStress PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        aporia_stage_engine_runtime(AporiaGraphicsStress)
        add_test(NAME AporiaGraphicsStress COMMAND AporiaGraphicsStress 4096 4)
        set_tests_properties(AporiaGraphicsStress PROPERTIES TIMEOUT 180)

        if(WIN32)
            add_executable(AporiaGraphicsNative tests/graphics/native_main.cpp)
            target_link_libraries(AporiaGraphicsNative PRIVATE Aporia::Engine user32)
            set_target_properties(AporiaGraphicsNative PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
            aporia_stage_engine_runtime(AporiaGraphicsNative)
            add_test(NAME AporiaGraphicsNative COMMAND AporiaGraphicsNative)
            set_tests_properties(AporiaGraphicsNative PROPERTIES TIMEOUT 60)
        endif()
    endif()
endif()

if (APORIA_BUILD_HOST_SAMPLE)
    add_executable(AporiaHostSample host_samples/headless/main.cpp)
    target_link_libraries(AporiaHostSample PRIVATE Aporia::Engine)
    set_target_properties(AporiaHostSample PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    aporia_stage_engine_runtime(AporiaHostSample)

    if(APORIA_ENABLE_GRAPHICS AND WIN32)
        add_executable(AporiaNativeHostSample host_samples/native/main.cpp)
        target_link_libraries(AporiaNativeHostSample PRIVATE Aporia::Engine user32)
        set_target_properties(AporiaNativeHostSample PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        aporia_stage_engine_runtime(AporiaNativeHostSample)
    endif()
endif()
