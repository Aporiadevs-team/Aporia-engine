#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "TextureStore.hpp"

#include "Graphics/GlobalDrawer/Support/Support.hpp"

#include <cstdio>
#include <limits>
#include <utility>

namespace Aporia::graphics_internal {

namespace {

TextureManager::CreateContext makeCreateContext(const TextureStore::Device& device) {
    TextureManager::CreateContext context;
    context.device = device.device;
    context.physicalDevice = device.physicalDevice;
    context.memoryProperties = device.memoryProperties;
    context.commandBuffer = device.commandBuffer;
    context.queue = device.queue;
    return context;
}

} // namespace

bool TextureStore::uploadData(Resource& texture, const void* pixels, std::size_t dataSize, VkImageLayout oldLayout, std::string& error) {
    TextureManager::UploadContext context;
    context.device = device_.device;
    context.physicalDevice = device_.physicalDevice;
    context.memoryProperties = device_.memoryProperties;
    context.commandBuffer = device_.commandBuffer;
    context.queue = device_.queue;
    if (!TextureManager::uploadTexture(context, texture, pixels, dataSize, oldLayout)) {
        error = "Texture upload failed";
        return false;
    }
    ++textureUploadCount_;
    return true;
}

bool TextureStore::createResource(const TextureDesc& description, Resource& texture, std::string& error) {
    if (device_.device == VK_NULL_HANDLE || device_.commandBuffer == VK_NULL_HANDLE ||
        description.width == 0 || description.height == 0 || description.pixels == nullptr ||
        description.dataSize == 0) {
        error = "Texture description is invalid";
        return false;
    }

    VkFormat format = VK_FORMAT_UNDEFINED;
    std::uint32_t bytesPerPixel = 0;
    if (!textureFormatInfo(description.format, format, bytesPerPixel)) {
        error = "Texture format is unsupported";
        return false;
    }
    if (description.width > 16384 || description.height > 16384 ||
        description.width > std::numeric_limits<std::size_t>::max() / description.height ||
        description.width * description.height > std::numeric_limits<std::size_t>::max() / bytesPerPixel ||
        description.dataSize != description.width * description.height * bytesPerPixel) {
        error = "Texture dimensions or data size are invalid";
        return false;
    }

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(device_.physicalDevice, format, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0 ||
        (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_DST_BIT) == 0) {
        error = "Texture format does not support sampling and uploads";
        return false;
    }

    if (!TextureManager::createResource(
            makeCreateContext(device_), description, format, bytesPerPixel, textureScalePercent_, texture)) {
        error = "Texture resource creation failed";
        return false;
    }
    ++textureUploadCount_;
    return true;
}

bool TextureStore::createDescriptorResources(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device_.device, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        error = "vkCreateDescriptorSetLayout failed";
        return false;
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = kMaxDescriptorSets;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // Per-texture sets are handed out lazily and freed when their texture is
    // destroyed, so the pool must allow individual frees.
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = kMaxDescriptorSets;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device_.device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        error = "vkCreateDescriptorPool failed";
        return false;
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout_;
    if (vkAllocateDescriptorSets(device_.device, &allocateInfo, &descriptorSet_) != VK_SUCCESS) {
        error = "vkAllocateDescriptorSets failed";
        return false;
    }

    const auto textureIt = textures_.find(defaultTexture_.value());
    if (textureIt == textures_.end()) {
        error = "Default texture is unavailable";
        return false;
    }
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = textureIt->second.sampler;
    imageInfo.imageView = textureIt->second.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_.device, 1, &write, 0, nullptr);
    return true;
}

VkDescriptorSet TextureStore::setFor(TextureHandle handle) {
    if (descriptorPool_ == VK_NULL_HANDLE || !handle.valid()) {
        return VK_NULL_HANDLE;
    }
    const auto existing = descriptorSets_.find(handle.value());
    if (existing != descriptorSets_.end()) {
        return existing->second;
    }
    const auto textureIt = textures_.find(handle.value());
    if (textureIt == textures_.end()) {
        return VK_NULL_HANDLE;
    }
    if (descriptorSets_.size() + 1 >= kMaxDescriptorSets) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorSetLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_.device, &allocateInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = textureIt->second.sampler;
    imageInfo.imageView = textureIt->second.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_.device, 1, &write, 0, nullptr);
    descriptorSets_.emplace(handle.value(), set);
    return set;
}

void TextureStore::destroyDescriptorResources(VkDevice device) noexcept {
    if (device != VK_NULL_HANDLE) {
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, descriptorSetLayout_, nullptr);
        }
    }
    descriptorPool_ = VK_NULL_HANDLE;
    descriptorSetLayout_ = VK_NULL_HANDLE;
    descriptorSet_ = VK_NULL_HANDLE;
}

bool TextureStore::initialize(SceneBindings& scene, std::string& error) {
    (void)scene;
    Resource texture;
    if (!TextureManager::createSolid(makeCreateContext(device_), 255, 255, 255, 255, texture)) {
        error = "Default texture creation failed";
        return false;
    }
    ++textureUploadCount_;
    const std::uint64_t id = nextTextureId_++;
    try {
        const auto result = textures_.emplace(id, std::move(texture));
        if (!result.second) {
            error = "Default texture handle allocation failed";
            return false;
        }
    } catch (...) {
        error = "Default texture allocation failed";
        return false;
    }
    defaultTexture_ = TextureHandle{id};
    boundTexture_ = defaultTexture_;
    return createDescriptorResources(error);
}

bool TextureStore::bind(TextureHandle handle, SceneBindings& scene, std::uint32_t frameSlot, std::string& error) {
    if (descriptorSet_ == VK_NULL_HANDLE || !handle.valid()) {
        error = "Texture descriptor is not available";
        return false;
    }
    const auto textureIt = textures_.find(handle.value());
    if (textureIt == textures_.end()) {
        error = "Texture handle is invalid";
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = textureIt->second.sampler;
    imageInfo.imageView = textureIt->second.view;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device_.device, 1, &write, 0, nullptr);
    boundTexture_ = handle;
    scene.bindAlbedo(frameSlot, textureIt->second.view, textureIt->second.sampler);
    return true;
}

TextureHandle TextureStore::create(const TextureDesc& description, SceneBindings& scene, std::string& error) {
    
    if (nextTextureId_ == 0) {
        error = "Texture handle space is exhausted";
        return invalidTextureHandle;
    }

    Resource texture;
    if (!createResource(description, texture, error)) {
        
        return invalidTextureHandle;
    }
    

    const std::uint64_t id = nextTextureId_++;
    if (textures_.find(id) != textures_.end()) {
        TextureManager::destroyResource(device_.device, texture);
        error = "Texture handle allocation failed";
        return invalidTextureHandle;
    }
    try {
        const auto result = textures_.emplace(id, std::move(texture));
        if (!result.second) {
            TextureManager::destroyResource(device_.device, texture);
            error = "Texture handle allocation failed";
            return invalidTextureHandle;
        }
    } catch (...) {
        const auto textureIt = textures_.find(id);
        if (textureIt != textures_.end()) {
            TextureManager::destroyResource(device_.device, textureIt->second);
            textures_.erase(textureIt);
        } else {
            TextureManager::destroyResource(device_.device, texture);
        }
        error = "Texture resource allocation failed";
        return invalidTextureHandle;
    }

    const TextureHandle handle{id};
    if (!bind(handle, scene, 0, error)) {
        
        const auto textureIt = textures_.find(id);
        if (textureIt != textures_.end()) {
            TextureManager::destroyResource(device_.device, textureIt->second);
            textures_.erase(textureIt);
        }
        return invalidTextureHandle;
    }
    
    return handle;
}

bool TextureStore::update(TextureHandle handle, const void* pixels, std::size_t dataSize, std::string& error) {
    if (!handle.valid()) {
        error = "Texture handle is invalid";
        return false;
    }
    const auto textureIt = textures_.find(handle.value());
    if (textureIt == textures_.end()) {
        error = "Texture handle is invalid";
        return false;
    }
    return uploadData(textureIt->second, pixels, dataSize, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, error);
}

void TextureStore::destroy(TextureHandle handle, SceneBindings& scene, std::string& error) {
    if (!handle.valid()) {
        return;
    }
    if (handle == defaultTexture_) {
        error = "Default texture cannot be destroyed";
        return;
    }
    const auto textureIt = textures_.find(handle.value());
    if (textureIt == textures_.end()) {
        error = "Texture handle is invalid";
        return;
    }
    // Destroy requires that no in-flight GPU command references the texture.
    // vkDeviceWaitIdle is necessary here — we have no access to the per-frame
    // fence from TextureStore, and destroying a live resource would be UB.
    if (device_.device == VK_NULL_HANDLE || vkDeviceWaitIdle(device_.device) != VK_SUCCESS) {
        error = "vkDeviceWaitIdle(texture destroy) failed";
        return;
    }
    if (boundTexture_ == handle && !defaultTexture_.valid()) {
        error = "Default texture is unavailable";
        return;
    }
    if (boundTexture_ == handle && !bind(defaultTexture_, scene, 0, error)) {
        return;
    }
    TextureManager::destroyResource(device_.device, textureIt->second);
    textures_.erase(textureIt);
    const auto setIt = descriptorSets_.find(handle.value());
    if (setIt != descriptorSets_.end()) {
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(device_.device, descriptorPool_, 1, &setIt->second);
        }
        descriptorSets_.erase(setIt);
    }
    const auto boundIt = textures_.find(boundTexture_.value());
    if (boundIt != textures_.end()) {
        // After vkDeviceWaitIdle all slots are safe.
        scene.bindAlbedo(0, boundIt->second.view, boundIt->second.sampler);
    }
}

void TextureStore::release(VkDevice device) noexcept {
    for (auto& entry : textures_) {
        TextureManager::destroyResource(device, entry.second);
    }
    textures_.clear();
    descriptorSets_.clear(); // pool destruction frees the sets
    defaultTexture_ = invalidTextureHandle;
    boundTexture_ = invalidTextureHandle;
    nextTextureId_ = 1;
    textureUploadCount_ = 0;
    destroyDescriptorResources(device);
}

} // namespace Aporia::graphics_internal
