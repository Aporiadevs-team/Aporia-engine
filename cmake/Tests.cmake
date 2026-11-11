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
    foreach(n IN LISTS 7;9;12;13)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/extrasTest/${n}/CMakeLists.txt")
            add_subdirectory("tests/extrasTest/${n}" "aporia-render-test-${n}")
        endif()
    endforeach()
endif()

if(APORIA_BUILD_TESTS)
    enable_testing()

    # scompileWithBuild — core tests compiled alongside the engine
    aporia_add_host_executable(AporiaCli tests/scompileWithBuild/cli_main.cpp)
    add_test(NAME AporiaCli COMMAND AporiaCli)

    if(APORIA_ENABLE_GRAPHICS)
        aporia_add_host_executable(AporiaGraphicsCli tests/scompileWithBuild/graphics_main.cpp)
        add_test(NAME AporiaGraphicsCli COMMAND AporiaGraphicsCli)

        aporia_add_host_executable(AporiaGraphicsStress tests/scompileWithBuild/graphics_main_stress.cpp psapi)
        add_test(NAME AporiaGraphicsStress COMMAND AporiaGraphicsStress 4096 4)
        set_tests_properties(AporiaGraphicsStress PROPERTIES TIMEOUT 180)

        aporia_add_host_executable(AporiaTextTest tests/scompileWithBuild/text_main.cpp)
        add_test(NAME AporiaTextTest COMMAND AporiaTextTest 6000)
        set_tests_properties(AporiaTextTest PROPERTIES TIMEOUT 300)

        if(APORIA_EXPERIMENTAL)
            aporia_add_host_executable(AporiaGraphicsMtTest tests/scompileWithBuild/graphics_mt_main.cpp)
            add_test(NAME AporiaGraphicsMtTest COMMAND AporiaGraphicsMtTest 600)
            set_tests_properties(AporiaGraphicsMtTest PROPERTIES TIMEOUT 180)
        endif()

        if(WIN32)
            aporia_add_host_executable(AporiaGraphicsNative tests/scompileWithBuild/graphics_native_main.cpp user32)
            add_test(NAME AporiaGraphicsNative COMMAND AporiaGraphicsNative)
            set_tests_properties(AporiaGraphicsNative PROPERTIES TIMEOUT 60)
        endif()
    endif()
endif()
