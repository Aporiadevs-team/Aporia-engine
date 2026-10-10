# Compile every shader under shaders/{world,ui,post_process} to SPIR-V.
# Requires the Vulkan SDK (glslangValidator). Defines AporiaVulkanShaders and
# exports APORIA_VULKAN_SHADER_DIR (where the .spv files land).

if(NOT TARGET Vulkan::Vulkan)
    find_package(Vulkan REQUIRED)
endif()

set(APORIA_VULKAN_SHADER_DIR "${CMAKE_BINARY_DIR}/vulkan-shaders")
set(APORIA_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_process")

set(APORIA_GLSL_DEFINES)
if(APORIA_EXPERIMENTAL)
    set(APORIA_GLSL_DEFINES -DAPORIA_EXPERIMENTAL=1)
endif()

file(GLOB_RECURSE APORIA_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/world/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_process/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_process/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/post_process/*.comp")
if(NOT APORIA_EXPERIMENTAL)
    list(FILTER APORIA_SHADER_SOURCES EXCLUDE REGEX "volumetric_inject[.]comp$")
endif()

# Shared #include files (post_process/*.glsl) are dependencies of every shader.
set(APORIA_SHADER_INCLUDES
    "${APORIA_SHADER_INCLUDE_DIR}/common.glsl"
    "${APORIA_SHADER_INCLUDE_DIR}/bloom.glsl"
    "${APORIA_SHADER_INCLUDE_DIR}/fog.glsl"
    "${APORIA_SHADER_INCLUDE_DIR}/godrays.glsl"
    "${APORIA_SHADER_INCLUDE_DIR}/aa.glsl")

set(APORIA_VULKAN_SHADER_OUTPUTS)
foreach(shader_source IN LISTS APORIA_SHADER_SOURCES)
    get_filename_component(shader_name "${shader_source}" NAME)
    set(shader_output "${APORIA_VULKAN_SHADER_DIR}/${shader_name}.spv")
    add_custom_command(
        OUTPUT "${shader_output}"
        COMMAND "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            -V "${shader_source}"
            "-I${APORIA_SHADER_INCLUDE_DIR}"
            ${APORIA_GLSL_DEFINES}
            -o "${shader_output}"
        DEPENDS "${shader_source}" ${APORIA_SHADER_INCLUDES}
        VERBATIM)
    list(APPEND APORIA_VULKAN_SHADER_OUTPUTS "${shader_output}")
endforeach()

add_custom_target(AporiaVulkanShaders DEPENDS ${APORIA_VULKAN_SHADER_OUTPUTS})
