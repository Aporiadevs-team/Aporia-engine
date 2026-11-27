#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Aporia::graphics_internal {

// Owns every VkShaderModule the renderer creates.
//
// Modules are compiled once from '<directory>/<name>.spv' and cached by name,
// so several pipelines can share one module and no renderer has to create or
// destroy shader modules itself. The manager outlives every pipeline; call
// destroy() after the pipelines that reference the modules are gone.
class ShaderManager {
public:
    // Directory holding the compiled SPIR-V files. A trailing slash is dropped.
    void setDirectory(std::string directory);
    [[nodiscard]] const std::string& directory() const noexcept { return directory_; }

    // Returns the cached module for `name` (the file name without `.spv`), or
    // creates it. VK_NULL_HANDLE on failure; `error` receives the reason.
    VkShaderModule load(VkDevice device, std::string_view name, std::string& error);
    VkShaderModule load(VkDevice device, std::string_view name);

    [[nodiscard]] std::size_t moduleCount() const noexcept { return entries_.size(); }

    // Destroys every module. Safe on null handles and an empty manager.
    void destroy(VkDevice device) noexcept;

private:
    struct Entry {
        std::string name;
        VkShaderModule module = VK_NULL_HANDLE;
    };

    std::string directory_ = "vulkan-shaders";
    std::vector<Entry> entries_;
};

} // namespace Aporia::graphics_internal
