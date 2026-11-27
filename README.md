# Aporia Engine — DirectX backend

A Windows-only, C++20 / CMake engine with a fixed-timestep physics core and a
**version-agnostic graphics stack**. This branch replaces the Vulkan-only
renderer with a backend abstraction that runs on Direct3D 12, Direct3D 11 and
Direct3D 9, so the same high-level rendering code (lighting, shadows, materials,
terrain) drives every DirectX generation without recompiling or forking the
renderer.

The engine builds as a single shared library (`AporiaEngine.dll`) with a public
API under `include/Aporia`; host applications link against the
`Aporia::Engine` target.

## Why three DirectX versions

Vulkan is the modern baseline but it is not universal: older machines — some
thin clients, pre-Windows 10 hardware, retired drivers — only expose D3D9 or
D3D11. Rather than maintaining three renderers, the engine keeps one renderer and
three thin backends behind a single interface:

| Backend | Target hardware | Notes |
| --- | --- | --- |
| D3D12 | Modern GPUs | Compute, bindless-ish resource heaps, full post-processing chain. |
| D3D11 | Mid-range and older DX11 machines | No compute; simplified shadows and post effects. |
| D3D9 | Legacy hardware | Shader Model 3, fixed-function fallbacks, no compute, no MRT. |

The renderer never asks *which API* it is running on. It asks the backend for its
capabilities (`BackendCapabilities`) and picks a path: the D3D9 path drops the
compute-driven volumetric fog, falls back to a single-sample shadow filter and
skips the HDR resolve, while D3D12 runs the full chain.

## Architecture

```
src/Device/Backend/          API-agnostic contract
  BackendTypes.hpp           handles, enums, descriptors (no D3D/Vulkan types)
  BackendCapabilities.hpp    feature query every backend implements
  GraphicsBackend.hpp        the interface the renderer talks to
  BackendFactory.cpp         picks a backend from Aporia::GraphicsConfig
src/Device/D3D12/            Direct3D 12 backend
src/Device/D3D11/            Direct3D 11 backend
src/Device/D3D9/             Direct3D 9 backend
src/Graphics/                high-level renderer, shared by all backends
```

A backend owns device, swapchain, command recording, buffers, textures,
samplers, shaders and pipelines. It exposes one flat recording API
(`beginRenderPass`, `setPipeline`, `draw`, `endRenderPass`) that maps onto
D3D12 command lists, D3D11 immediate-context calls and D3D9 immediate mode.

## Quick start

Grab `create_project.bat` from the latest release, drop it anywhere and run:

```bat
create_project.bat my_game
```

It clones the released engine into `my_game\engine` and writes a base
`CMakeLists.txt` plus `src\main.cpp` that already links `Aporia::Engine`.
Then:

```sh
cd my_game
cmake -S . -B build -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_D3D12=ON
cmake --build build --config Release
```

## Requirements

- Windows 10/11, x64
- CMake 3.25+
- Visual Studio (MSVC) with C++20
- DirectX 12/11/9: no extra SDK, the Windows SDK ships `d3d12.lib`,
  `d3d11.lib`, `d3d9.lib` and `d3dcompiler_47.dll`
- Optional: SDL3 or GLFW for the SDL3/GLFW input backends

## Build

```sh
cmake -S . -B build -DAPORIA_ENABLE_GRAPHICS=ON -DAPORIA_ENABLE_D3D12=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### CMake options

| Option | Default | Description |
| --- | --- | --- |
| `APORIA_ENABLE_PHYSICS` | ON | Physics module (`Aporia::PhysicsWorld`, terrain). |
| `APORIA_ENABLE_INPUT` | ON | Input module (Win32 always; SDL3/GLFW optional). |
| `APORIA_INPUT_SDL3` / `APORIA_INPUT_GLFW` | OFF | Extra input backends. |
| `APORIA_ENABLE_GRAPHICS` | OFF | Graphics module. |
| `APORIA_ENABLE_D3D12` | OFF | Direct3D 12 backend. |
| `APORIA_ENABLE_D3D11` | OFF | Direct3D 11 backend. |
| `APORIA_ENABLE_D3D9` | OFF | Direct3D 9 backend. |
| `APORIA_BACKEND` | `auto` | `auto`, `d3d12`, `d3d11`, `d3d9` — backend used at runtime. |
| `APORIA_BUILD_TESTS` | ON (top level) | CLI/render/text test runners. |
| `APORIA_BUILD_HOST_SAMPLE` | ON (top level) | Headless/native host samples. |
| `APORIA_BUILD_RENDER_TESTS` | OFF | Extra windowed render test projects. |
| `APORIA_BUILD_VISIBLE_RENDER_TESTS` | OFF | Interactive visible render test. |

## Layout

- `include/Aporia/` — public headers (`Game`, `Scene`, `Graphics`, `Physics`,
  `Math`, `Input`, `Terrain`, `City`). No D3D or Vulkan types leak in here.
- `src/Device/Backend/` — the backend contract shared by every DirectX version.
- `src/Device/D3D12|D3D11|D3D9/` — version-specific implementations.
- `src/Graphics/` — high-level renderer: lighting, shadows, materials, meshes,
  textures, terrain, UI and text, written against the backend interface only.
- `src/Physics/`, `src/Terrain/` — physics core and terrain generation.
- `shaders/` — shader sources compiled per backend.
- `tests/` — CLI, graphics, text and text-GUI test runners.
- `host_samples/` — minimal headless and native hosts.

## Tests

`ctest` registers:

- `AporiaCli` — core/physics CLI runner.
- `AporiaGraphicsCli` — headless graphics smoke (draw, AS, LOD, materials, AA).
- `AporiaGraphicsStress` — repeated init/shutdown and bounded-memory frame loop.
- `AporiaTextTest` / `AporiaTextGuiTest` — TTF/MSDF text throughput.
- `AporiaGraphicsNative` — Win32 surface / swapchain presentation.

## Branches

- `main` — stable Vulkan release (`v1.0.0`).
- `directx` — this branch: DirectX 12/11/9 backend with the shared renderer.
- `experimental/dev` — experimental features behind `APORIA_EXPERIMENTAL`.

