# GlobalDrawer

`GlobalDrawer` is the Vulkan scene-rendering facade. Its internal ownership is
split by responsibility:

- `Device/`: Vulkan instance, device, queue, command buffer and presentation.
- `Meshes/`: mesh upload and geometry pipelines.
- `Shadows/`: depth maps, PCF/PCSS/contact shadow policy and future cascades.
- `Hdr/`: HDR targets, anti-aliasing and tone mapping.
- `Text/`: TTF/MSDF atlas batches and glyph layout.
- `Rectangulars/`: SDF rounded rectangles and UI composition.
- `TextureManager/`: texture handles, uploads and sampler resources.

`Graphics` remains the public facade. `Game` and `Scene` stay outside this
tree: they own runtime orchestration and scene data, while the drawer consumes
render descriptions from them.
