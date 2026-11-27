cmake_minimum_required(VERSION 3.25)
project(MyAporiaProject LANGUAGES CXX)

# The engine builds as a subproject: no engine tests, no host samples.
set(APORIA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(APORIA_BUILD_HOST_SAMPLE OFF CACHE BOOL "" FORCE)

add_subdirectory(engine)

add_executable(my_game src/main.cpp)
target_link_libraries(my_game PRIVATE Aporia::Engine)

# Put AporiaEngine.dll next to the executable so it can be run directly.
# Defined by the engine in cmake/Runtime.cmake.
aporia_stage_engine_runtime(my_game)
