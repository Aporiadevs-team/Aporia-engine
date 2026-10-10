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
    foreach(n RANGE 1 13)
        add_subdirectory("tests/graphics/${n}" "aporia-render-test-${n}")
    endforeach()
endif()

if(APORIA_BUILD_VISIBLE_RENDER_TESTS)
    if(NOT WIN32 OR NOT APORIA_ENABLE_GRAPHICS)
        message(FATAL_ERROR "APORIA_BUILD_VISIBLE_RENDER_TESTS requires Windows and graphics")
    endif()
    aporia_add_host_executable(AporiaRenderTestVisible tests/graphics/visible_main.cpp user32)
endif()

if(APORIA_BUILD_TESTS)
    enable_testing()

    aporia_add_host_executable(AporiaCli tests/cli/main.cpp)
    add_test(NAME AporiaCli COMMAND AporiaCli)

    if(APORIA_ENABLE_GRAPHICS)
        aporia_add_host_executable(AporiaGraphicsCli tests/graphics/main.cpp)
        add_test(NAME AporiaGraphicsCli COMMAND AporiaGraphicsCli)

        aporia_add_host_executable(AporiaGraphicsStress tests/graphics/main_stress.cpp psapi)
        add_test(NAME AporiaGraphicsStress COMMAND AporiaGraphicsStress 4096 4)
        set_tests_properties(AporiaGraphicsStress PROPERTIES TIMEOUT 180)

        aporia_add_host_executable(AporiaTextTest tests/text/main.cpp)
        add_test(NAME AporiaTextTest COMMAND AporiaTextTest 6000)
        set_tests_properties(AporiaTextTest PROPERTIES TIMEOUT 300)

        if(APORIA_EXPERIMENTAL)
            aporia_add_host_executable(AporiaGraphicsMtTest tests/graphics_mt/main.cpp)
            add_test(NAME AporiaGraphicsMtTest COMMAND AporiaGraphicsMtTest 600)
            set_tests_properties(AporiaGraphicsMtTest PROPERTIES TIMEOUT 180)
        endif()

        if(WIN32)
            add_subdirectory(tests/text_gui aporia-text-gui-test)
            add_test(NAME AporiaTextGuiTest COMMAND AporiaTextGuiTest 8)
            set_tests_properties(AporiaTextGuiTest PROPERTIES TIMEOUT 120)

            aporia_add_host_executable(AporiaGraphicsNative tests/graphics/native_main.cpp user32)
            add_test(NAME AporiaGraphicsNative COMMAND AporiaGraphicsNative)
            set_tests_properties(AporiaGraphicsNative PROPERTIES TIMEOUT 60)
        endif()
    endif()
endif()
