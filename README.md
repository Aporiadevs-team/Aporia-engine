# Aporia Engine

A Windows-only, C++20 / CMake engine built around a Vulkan scene renderer and a
fixed-timestep physics core. The engine builds as a single shared library
(`AporiaEngine.dll`) with a public API under `include/Aporia`; host applications
link against the `Aporia::Engine` target.

## Requirements

- Windows 10/11, x64
- CMake 3.25+
- Visual Studio (MSVC) with C++20
- Vulkan SDK (required to build the graphics backend; provides `glslangValidator`
  used to compile the shaders in `shaders/`)
- Optional: SDL3 or GLFW for the SDL3/GLFW input backends

## Build

```sh
cmake -S . -B build -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_VULKAN=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The graphics backend requires the Vulkan backend (`APORIA_ENABLE_VULKAN`
implies `APORIA_ENABLE_GRAPHICS`). Shaders are compiled to SPIR-V at build time
into `build/vulkan-shaders`.

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `APORIA_ENABLE_PHYSICS` | ON | Physics module (`Aporia::PhysicsWorld`, terrain). |
| `APORIA_ENABLE_INPUT` | ON | Input module (Win32 always; SDL3/GLFW optional). |
| `APORIA_INPUT_SDL3` / `APORIA_INPUT_GLFW` | OFF | Extra input backends. |
| `APORIA_ENABLE_GRAPHICS` | OFF | Graphics module. |
| `APORIA_ENABLE_VULKAN` | OFF | Vulkan backend for graphics. |
| `APORIA_EXPERIMENTAL` | OFF | Unstable fast-path; see Branches. |
| `APORIA_BUILD_TESTS` | ON (top level) | CLI/render/text test runners. |
| `APORIA_BUILD_HOST_SAMPLE` | ON (top level) | Headless/native host samples. |
| `APORIA_BUILD_RENDER_TESTS` | OFF | Extra windowed render test projects. |
| `APORIA_BUILD_VISIBLE_RENDER_TESTS` | OFF | Interactive visible render test. |

## Layout

- `include/Aporia/` — public headers (`Game`, `Scene`, `Graphics`, `Physics`,
  `Math`, `Input`, `Terrain`, `City`).
- `src/Device/` — Vulkan/render management: instance/device/queue, swapchain,
  frame acquire/submit/present, command pools, pipelines, readback, platform
  windows (`Platform/Win32|SDL3|Glfw`), `SceneManager` targets and the
  experimental `Threading` worker pool.
- `src/Graphics/` — graphics domain: `GlobalDrawer` (Quality, Smooth, Meshes,
  LOD, Shadows, Hdr, Text, Rectangulars, Materials, Support) and
  `TextureManager` (textures, samplers, handles).
- `src/Physics/`, `src/Terrain/` — physics core and terrain generation.
- `shaders/` — GLSL sources compiled to SPIR-V.
- `tests/` — CLI, graphics, text and text-GUI test runners.
- `host_samples/` — minimal headless and native hosts.

`Graphics` is the public facade. `Graphics::Impl` (private header
`GraphicsImpl.hpp`) owns the device/render state; the facade methods and the
per-domain implementation are split across the packages above so that
`Graphics.cpp` stays a thin assembler. See
`src/Graphics/GlobalDrawer/README.md` for the graphics ownership map.

## Tests

`ctest` registers:

- `AporiaCli` — core/physics CLI runner.
- `AporiaGraphicsCli` — headless graphics smoke (draw, AS, LOD, materials, AA).
- `AporiaGraphicsStress` — repeated init/shutdown and bounded-memory frame loop.
- `AporiaTextTest` / `AporiaTextGuiTest` — TTF/MSDF text throughput.
- `AporiaGraphicsNative` — Win32 surface / swapchain presentation.

## Branches

- `main` — stable release; builds with `APORIA_EXPERIMENTAL=OFF`.
- `experimental/dev` — experimental features behind `APORIA_EXPERIMENTAL`
  (currently: multi-layer per-item materials and the multithreaded renderer).
