# Collect engine sources into APORIA_SOURCES.
#
# Sources are globbed per module; CONFIGURE_DEPENDS re-runs CMake when files are
# added or removed. Each module only pulls its own directories, so the
# APORIA_ENABLE_* options still decide what is compiled.

file(GLOB APORIA_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Core holds the scene graph and the game facade.
file(GLOB_RECURSE APORIA_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/*.cpp")
list(APPEND APORIA_SOURCES ${APORIA_CORE_SOURCES})

if(APORIA_ENABLE_INPUT)
    # Win32 input source is always compiled.
    file(GLOB APORIA_INPUT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Input/Platform/Win32/Input/*.cpp")
    list(APPEND APORIA_SOURCES ${APORIA_INPUT_SOURCES})

    # SDL3 / GLFW input sources are NOT compiled in the main build — they
    # require SDL3/GLFW headers which are only fetched in the render-test
    # CMakeLists.txt files.  If you need SDL3/GLFW in the main build, add
    # FetchContent (or use a system installation) and re-enable the GLOB below.

    # if(APORIA_INPUT_SDL3)
    #     file(GLOB APORIA_INPUT_SDL3_SOURCES CONFIGURE_DEPENDS
    #         "${CMAKE_CURRENT_SOURCE_DIR}/src/Input/Platform/SDL3/Input/*.cpp")
    #     list(APPEND APORIA_SOURCES ${APORIA_INPUT_SDL3_SOURCES})
    # endif()

    # if(APORIA_INPUT_GLFW)
    #     file(GLOB APORIA_INPUT_GLFW_SOURCES CONFIGURE_DEPENDS
    #         "${CMAKE_CURRENT_SOURCE_DIR}/src/Input/Platform/Glfw/Input/*.cpp")
    #     list(APPEND APORIA_SOURCES ${APORIA_INPUT_GLFW_SOURCES})
    # endif()
endif()

if(APORIA_ENABLE_PHYSICS)
    file(GLOB_RECURSE APORIA_PHYSICS_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Physics/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Terrain/*.cpp")
    list(APPEND APORIA_SOURCES ${APORIA_PHYSICS_SOURCES})
endif()

if(APORIA_ENABLE_GRAPHICS)
    # Recursively collect Device/ files, but EXCLUDE Platform/ (input backends
    # are only compiled when SDL3/GLFW headers are available — see top of file).
    file(GLOB_RECURSE APORIA_DEVICE_SUB CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/*.cpp")
    # Remove Platform/* from the list (SDL3/Glfw input sources).
    file(GLOB_RECURSE APORIA_DEVICE_PLATFORM CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Platform/*.cpp")
    list(REMOVE_ITEM APORIA_DEVICE_SUB ${APORIA_DEVICE_PLATFORM})
    # Remove duplicate Win32 source (already added above via APORIA_INPUT_SOURCES).
    file(GLOB APORIA_WIN32_INPUT CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Platform/Win32/Input/*.cpp")
    list(REMOVE_ITEM APORIA_DEVICE_SUB ${APORIA_WIN32_INPUT})
    # Remove duplicate Device.cpp (already in APORIA_DEVICE_SUB, keep once).
    file(GLOB APORIA_DEVICE_ROOT CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/*.cpp")
    list(REMOVE_ITEM APORIA_DEVICE_SUB ${APORIA_DEVICE_ROOT})
    list(APPEND APORIA_DEVICE_SUB ${APORIA_DEVICE_ROOT})

    # The Direct3D backends are compiled through their own option below, so
    # they must not be picked up by the device glob.
    file(GLOB_RECURSE APORIA_DEVICE_DIRECTX CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/D3D12/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/D3D11/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/D3D9/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Backend/*.cpp")
    list(REMOVE_ITEM APORIA_DEVICE_SUB ${APORIA_DEVICE_DIRECTX})

    file(GLOB_RECURSE APORIA_GRAPHICS_SUB CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Graphics/*.cpp")
    list(APPEND APORIA_SOURCES ${APORIA_DEVICE_SUB} ${APORIA_GRAPHICS_SUB})
endif()

# The Direct3D backends implement the abstract backend contract in
# include/Aporia/Graphics/Backend.hpp.
if(APORIA_ENABLE_D3D12)
    file(GLOB_RECURSE APORIA_D3D12_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/D3D12/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/Device/Backend/*.cpp")
    list(APPEND APORIA_SOURCES ${APORIA_D3D12_SOURCES})
endif()
