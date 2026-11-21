#include "ShaderManager.hpp"

#include <cstdint>
#include <fstream>
#include <utility>

namespace Aporia::graphics_internal {

namespace {

std::vector<std::uint32_t> readSpirvFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamoff byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(words.data()), byteCount)) {
        return {};
    }
    return words;
}

} // namespace

void ShaderManager::setDirectory(std::string directory) {
    while (!directory.empty() && (directory.back() == '/' || directory.back() == '\\')) {
        directory.pop_back();
    }
    directory_ = std::move(directory);
}

VkShaderModule ShaderManager::load(VkDevice device, std::string_view name, std::string& error) {
    if (device == VK_NULL_HANDLE) {
        error = "shader load needs a device";
        return VK_NULL_HANDLE;
    }
    for (const Entry& entry : entries_) {
        if (std::string_view(entry.name) == name) {
            return entry.module;
        }
    }

    const std::string path = directory_ + "/" + std::string(name) + ".spv";
    const std::vector<std::uint32_t> code = readSpirvFile(path);
    if (code.empty()) {
        error = "missing compiled shader ";
        error += path;
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(std::uint32_t);
    info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
        error = "vkCreateShaderModule(";
        error += std::string(name);
        error += ") failed";
        return VK_NULL_HANDLE;
    }
    entries_.push_back(Entry{std::string(name), module});
    return module;
}

VkShaderModule ShaderManager::load(VkDevice device, std::string_view name) {
    std::string ignored;
    return load(device, name, ignored);
}

void ShaderManager::destroy(VkDevice device) noexcept {
    if (device != VK_NULL_HANDLE) {
        for (const Entry& entry : entries_) {
            if (entry.module != VK_NULL_HANDLE) {
                vkDestroyShaderModule(device, entry.module, nullptr);
            }
        }
    }
    entries_.clear();
}

} // namespace Aporia::graphics_internal
