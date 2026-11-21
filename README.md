# Aporia Engine

A Windows-only, C++20 / CMake engine built around a Vulkan scene renderer and a
fixed-timestep physics core. It ships as a single shared library
(`AporiaEngine.dll`) with a public API under `include/Aporia`; host applications
link against the `Aporia::Engine` target and own their window, input loop and
game rules.

**Version:** 0.1.0 (development line).

## Integration

The engine is a normal CMake subproject; the host keeps its own window and loop:

```cmake
add_subdirectory(engine)
add_executable(my_game src/main.cpp)
target_link_libraries(my_game PRIVATE Aporia::Engine)
```

```sh
cmake -S . -B build -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_VULKAN=ON
cmake --build build --config Release
```

After `add_subdirectory(engine)` the helper `aporia_stage_engine_runtime(my_game)`
is available; it copies `AporiaEngine.dll` next to the host executable as a
post-build step. A packaged starter script (`create_project.bat`) exists only on
the release line, not on this branch.

## Requirements

- Windows 10/11, x64
- CMake 3.25+
- Visual Studio (MSVC) with C++20
- Vulkan SDK for the graphics backend (provides `glslangValidator`, used to
  compile `shaders/` to SPIR-V at build time)
- Optional: SDL3 or GLFW for extra input sources. SDL3 is fetched automatically
  for `AporiaTextGuiTest`.

## Build

```sh
cmake -S . -B build -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_VULKAN=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`APORIA_ENABLE_VULKAN` implies `APORIA_ENABLE_GRAPHICS`; the current graphics
module requires Vulkan. Shaders land in `build/vulkan-shaders`.

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `APORIA_ENABLE_PHYSICS` | ON | Physics module (`PhysicsWorld`, terrain colliders). |
| `APORIA_ENABLE_INPUT` | ON | Input module (Win32 always; SDL3/GLFW optional). |
| `APORIA_INPUT_SDL3` / `APORIA_INPUT_GLFW` | OFF | Extra input sources. |
| `APORIA_ENABLE_GRAPHICS` | OFF | Graphics module. |
| `APORIA_ENABLE_VULKAN` | OFF | Vulkan backend (required by graphics today). |
| `APORIA_EXPERIMENTAL` | OFF | Unstable fast path: multi-layer materials, multithreaded frame recording, UI layers, froxel fog. |
| `APORIA_SEASONS` | OFF | Seasons, weather-driven sky, precipitation and analytic fog. |
| `APORIA_BUILD_TESTS` | ON (top level) | CTest runners. |
| `APORIA_BUILD_HOST_SAMPLE` | ON (top level) | Headless and Win32 native host samples. |
| `APORIA_BUILD_RENDER_TESTS` | OFF | Windowed render tests 7, 9, 12, 13. |

`APORIA_ENABLE_VEHICLE`, `APORIA_ENABLE_SOFTBODY`, `APORIA_ENABLE_AUDIO`,
`APORIA_ENABLE_SCRIPTING` and `APORIA_ENABLE_NETWORKING` are reserved option
names; no module is implemented behind them yet.

## Layout

- `include/Aporia/` — public API: `Game`, `Body`, `Scene`, `Graphics`
  (+ `Weather`, `MeshPrimitives`), `Physics` (`PhysicsWorld`, `Camera`), `Math`,
  `Input`, `Terrain`, `City`.
- `src/Core/` — `Game` / `Scene` orchestration and the fixed-step loop.
- `src/Device/` — Vulkan bring-up and frame plumbing: `Init/`, `Frame/`,
  `Swapchain/`, `Readback/`, `Format/`, `MemoryManager/`.
- `src/Graphics/` — renderer facade (`Graphics.cpp`, `GraphicsImpl.hpp`,
  `GraphicsInit.cpp`) and the domain packages: `SceneManager/` (targets, passes,
  frame, time), `Hdr/`, `Shadows/`, `Sky/`, `Lights/`, `Meshes/`, `LOD/`,
  `Materials/`, `TextureManager/`, `Text/`, `UI/`, `AntiAliasing/`,
  `Volumetric/`, `Weather/`, `Threading/`, `Pipeline/`, `Shaders/`, `Quality/`,
  `Support/`. Ownership map: `src/Graphics/README.md`.
- `src/Input/Platform/` — `Win32/`, `SDL3/`, `Glfw/` input sources.
- `src/Physics/` — `World/` (+ `Camera/`) and `internal/` (contacts, raycast,
  helpers).
- `src/Terrain/` — heightfield generation and `Generation/City/`.
- `shaders/` — GLSL under `world/`, `ui/`, `post_process/`.
- `tests/` — `compileWithBuild/` runners, `extrasTest/` windowed render tests,
  `connectableTests/`.
- `host_samples/` — minimal headless and Win32 native hosts.

## Tests

`ctest` registers:

- `AporiaCli` — core/physics CLI runner.
- `AporiaGraphicsCli` — headless graphics smoke (draw, AS, LOD, materials, AA).
- `AporiaGraphicsStress` — repeated init/shutdown and bounded-memory frame loop.
- `AporiaTextTest` / `AporiaTextGuiTest` — TTF/MSDF text throughput.
- `AporiaGraphicsNative` — Win32 surface / swapchain presentation.
- `AporiaGraphicsMtTest` — multithreaded frame recording (only with
  `APORIA_EXPERIMENTAL`).

`APORIA_BUILD_RENDER_TESTS` additionally builds the windowed tests 7, 9, 12, 13.

## Branches

- `experimental/dev` — this development line (0.1.0). Experimental features live
  behind `APORIA_EXPERIMENTAL` / `APORIA_SEASONS` and graduate to the stable path
  once validated.
- `main` — release line (v1.0.0) with `create_project.bat`.
