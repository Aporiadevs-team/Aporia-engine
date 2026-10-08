#include "Graphics/GlobalDrawer/GraphicsImpl.hpp"

namespace Aporia {

void Graphics::Impl::renderTonemap(VkCommandBuffer cmd, VkFramebuffer target, std::uint32_t width, std::uint32_t height) {
    graphics_internal::HdrRenderer::TonemapContext context;
    context.commandBuffer = cmd;
    context.renderPass = renderPass;

    const std::uint32_t slotIndex = currentFrameSlot % maxFramesInFlight;
    const VkCommandBuffer commands[3] = {
        secondaryBuffers[slotIndex][kSecondaryTonemap],
        secondaryBuffers[slotIndex][kSecondaryUiRect],
        secondaryBuffers[slotIndex][kSecondaryUiText]};
    graphics_internal::HdrRenderer::renderTonemap(context, target, width, height, commands, 3);
}

} // namespace Aporia
