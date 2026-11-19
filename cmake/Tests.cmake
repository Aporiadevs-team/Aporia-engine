# Test runners, render tests and host samples.

if(APORIA_BUILD_HOST_SAMPLE)
    aporia_add_host_executable(AporiaHostSample host_samples/headless/main.cpp)
    if(APORIA_ENABLE_GRAPHICS AND WIN32)
        aporia_add_host_executable(AporiaNativeHostSample host_samples/native/main.cpp user32)
    endif()
endif()

if(APORIA_BUILD_RENDER_TESTS)
    if(NOT APORIA_ENABLE_GRAPHICS)
        message(FATAL_ERROR "APORIA_BUILD_RENDER_TESTS requires APORIA_ENABLE_GRAPHICS")
    endif()
    # extrasTest render tests 7, 9, 12, 13
    foreach(n IN ITEMS 7 9 12 13)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/extrasTest/${n}/CMakeLists.txt")
            add_subdirectory("tests/extrasTest/${n}" "aporia-render-test-${n}")
        endif()
    endforeach()
endif()

if(APORIA_BUILD_TESTS)
    enable_testing()

    # compileWithBuild — core tests compiled alongside the engine
    aporia_add_host_executable(AporiaCli tests/compileWithBuild/cli_main.cpp)
    add_test(NAME AporiaCli COMMAND AporiaCli)

    if(APORIA_ENABLE_GRAPHICS)
        aporia_add_host_executable(AporiaGraphicsCli tests/compileWithBuild/graphics_main.cpp)
        add_test(NAME AporiaGraphicsCli COMMAND AporiaGraphicsCli)

        aporia_add_host_executable(AporiaGraphicsStress tests/compileWithBuild/graphics_main_stress.cpp psapi)
        add_test(NAME AporiaGraphicsStress COMMAND AporiaGraphicsStress 4096 4)
        set_tests_properties(AporiaGraphicsStress PROPERTIES TIMEOUT 180)

        aporia_add_host_executable(AporiaTextTest tests/compileWithBuild/text_main.cpp)
        add_test(NAME AporiaTextTest COMMAND AporiaTextTest 6000)
        set_tests_properties(AporiaTextTest PROPERTIES TIMEOUT 300)

        # AporiaTextGuiTest — windowed text throughput test (SDL3).
        # Fetch SDL3 for the GUI test.
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

        aporia_add_host_executable(AporiaTextGuiTest tests/compileWithBuild/text_gui_main.cpp)
        target_link_libraries(AporiaTextGuiTest PRIVATE Aporia::Engine ${APORIA_SDL3_LIB})
        set_target_properties(AporiaTextGuiTest PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        )
        aporia_stage_engine_runtime(AporiaTextGuiTest)
        add_custom_command(TARGET AporiaTextGuiTest POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:${APORIA_SDL3_LIB}>"
                "$<TARGET_FILE_DIR:AporiaTextGuiTest>"
        )
        add_test(NAME AporiaTextGuiTest COMMAND AporiaTextGuiTest 10)
        set_tests_properties(AporiaTextGuiTest PROPERTIES TIMEOUT 120)

        if(APORIA_EXPERIMENTAL)
            aporia_add_host_executable(AporiaGraphicsMtTest tests/compileWithBuild/graphics_mt_main.cpp)
            add_test(NAME AporiaGraphicsMtTest COMMAND AporiaGraphicsMtTest 600)
            set_tests_properties(AporiaGraphicsMtTest PROPERTIES TIMEOUT 180)
        endif()

        if(WIN32)
            aporia_add_host_executable(AporiaGraphicsNative tests/compileWithBuild/graphics_native_main.cpp user32)
            add_test(NAME AporiaGraphicsNative COMMAND AporiaGraphicsNative)
            set_tests_properties(AporiaGraphicsNative PROPERTIES TIMEOUT 60)
        endif()
    endif()
endif()
