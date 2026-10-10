# Collect engine sources into APORIA_SOURCES.
#
# Sources are globbed per module; CONFIGURE_DEPENDS re-runs CMake when files are
# added or removed. Each module only pulls its own directories, so the
# APORIA_ENABLE_* options still decide what is compiled.

file(GLOB APORIA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

if(APORIA_ENABLE_INPUT)
    file(GLOB APORIA_INPUT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Platform/Win32/Input/*.cpp")
    if(APORIA_INPUT_SDL3)
        file(GLOB APORIA_INPUT_SDL3_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Platform/SDL3/Input/*.cpp")
        list(APPEND APORIA_INPUT_SOURCES ${APORIA_INPUT_SDL3_SOURCES})
    endif()
    if(APORIA_INPUT_GLFW)
        file(GLOB APORIA_INPUT_GLFW_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Platform/Glfw/Input/*.cpp")
        list(APPEND APORIA_INPUT_SOURCES ${APORIA_INPUT_GLFW_SOURCES})
    endif()
    list(APPEND APORIA_SOURCES ${APORIA_INPUT_SOURCES})
endif()

if(APORIA_ENABLE_PHYSICS)
    file(GLOB_RECURSE APORIA_PHYSICS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Terrain/*.cpp")
    list(APPEND APORIA_SOURCES ${APORIA_PHYSICS_SOURCES})
endif()

if(APORIA_ENABLE_GRAPHICS)
    # Device/ root is non-recursive so Device/Platform (input) stays out of here.
    file(GLOB APORIA_GRAPHICS_ROOT CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/*.cpp")
    file(GLOB_RECURSE APORIA_GRAPHICS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/MemoryManager/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/SceneManager/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Init/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Pipeline/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Readback/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Swapchain/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Frame/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Threading/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/*.cpp")
    list(APPEND APORIA_SOURCES ${APORIA_GRAPHICS_ROOT} ${APORIA_GRAPHICS_SOURCES})
endif()
