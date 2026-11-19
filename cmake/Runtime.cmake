# Copy the engine runtime (AporiaEngine.dll) next to a host/test executable.

function(aporia_stage_engine_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:AporiaEngine>"
            "$<TARGET_FILE_DIR:${target}>")
endfunction()

# Add an executable that links the engine and stages the DLL next to it.
function(aporia_add_host_executable target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE Aporia::Engine ${ARGN})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    aporia_stage_engine_runtime(${target})
endfunction()
