# GlobalDrawer

`GlobalDrawer` is the Vulkan scene-rendering facade. Ownership is split by
domain package:

- `Quality/`: preset table, `Graphics` quality/shadow settings, the
  `QualityTargets` adapters and `Impl::applyQualityPreset`.
- `Smooth/`: anti-aliasing mode/strength policy.
  - `Smooth/TAA/`: temporal resolve (`TaaRenderer`).
  - `Smooth/SMAA/`: shader-side morphological edge resolve (see `shaders/`).
- `Meshes/`: mesh upload, the mesh store and the mesh pipelines.
- `LOD/`: level-of-detail facade over the mesh store.
- `Shadows/`: depth maps, PCSS/PCF policy and the shadow pass.
- `Hdr/`: tone mapping and the HDR pass.
- `Text/`: TTF/MSDF atlases, glyph layout and the UI text batch.
- `Rectangulars/`: SDF rounded rectangles and UI draw-list settings.
- `Materials/`: experimental per-item multi-layer materials (set 1).
- `Support/`: shared helpers, the engine log and `Graphics::stats`.

Textures live in `src/Graphics/TextureManager/` (`TextureManager`,
`TextureStore`, texture handle facade). Device/render management lives in
`src/Device/` next to the swapchain: `Device/Init/` (bring-up), `Device/Frame/`
(frame acquire/submit/present and resizes), `Device/Pipeline/`, `Device/Readback/`,
`Device/Threading/` and `Device/SceneManager/`.

`Device/Threading/JobSystem` (experimental) is the main-thread owned worker pool
backing the multithreaded renderer; per-pass command buffers build on top of it.
The shadow pass records into its own command buffer/pool on a worker while the
main thread records the rest of the frame. Toggle it at runtime with
`Graphics::setMultithreadedRender`; benchmark with `AporiaGraphicsMtTest`.
Frames also use per-slot render command pools/buffers and fences (no
`vkQueueWaitIdle` per frame), so multiple frames stay in flight.

`Graphics.cpp` is a thin assembler (construction, lifecycle queries and the
camera/render entry points); `GraphicsImpl.hpp` declares the private
`Graphics::Impl` state. `Game` and `Scene` stay outside this tree: they own
runtime orchestration and scene data, while the drawer consumes render
descriptions from them.
