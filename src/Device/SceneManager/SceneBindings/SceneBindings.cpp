#include "SceneBindings.hpp"

#include <array>
#include <cstring>

namespace Aporia::graphics_internal {

namespace {

bool findMemoryType(
        const VkPhysicalDeviceMemoryProperties& properties,
        std::uint32_t typeFilter,
        VkMemoryPropertyFlags required,
        std::uint32_t& outType) noexcept {
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
        if ((typeFilter & (1u << index)) == 0) {
            continue;
        }
        if ((properties.memoryTypes[index].propertyFlags & required) == required) {
            outType = index;
            return true;
        }
    }
    return false;
}

} // namespace

bool SceneBindings::createLayout(VkDevice device) {
    device_ = device;
    if (layout_ != VK_NULL_HANDLE) {
        return true;
    }
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (std::uint32_t index = 0; index < 3; ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 4;
    layoutInfo.pBindings = bindings;
    return vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout_) == VK_SUCCESS;
}

bool SceneBindings::allocate(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memoryProperties,
        VkDeviceSize uniformSize,
        VkImageView albedoView,
        VkSampler albedoSampler,
        VkImageView shadowView,
        VkSampler shadowSampler) {
    device_ = device;
    if (layout_ == VK_NULL_HANDLE && !createLayout(device)) {
        return false;
    }
    releaseResources(device);
    uniformSize_ = uniformSize;

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = kSlots;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = kSlots * 3;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kSlots;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool_) != VK_SUCCESS) {
        return false;
    }

    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = uniformSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffers_[slot]) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device, buffers_[slot], &requirements);
        std::uint32_t memoryType = 0;
        if (!findMemoryType(memoryProperties, requirements.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                            memoryType)) {
            return false;
        }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = memoryType;
        if (vkAllocateMemory(device, &allocateInfo, nullptr, &memory_[slot]) != VK_SUCCESS ||
            vkBindBufferMemory(device, buffers_[slot], memory_[slot], 0) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorSetAllocateInfo allocateSetInfo{};
        allocateSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateSetInfo.descriptorPool = pool_;
        allocateSetInfo.descriptorSetCount = 1;
        allocateSetInfo.pSetLayouts = &layout_;
        if (vkAllocateDescriptorSets(device, &allocateSetInfo, &sets_[slot]) != VK_SUCCESS) {
            return false;
        }

        VkDescriptorBufferInfo bufferDescriptor{};
        bufferDescriptor.buffer = buffers_[slot];
        bufferDescriptor.offset = 0;
        bufferDescriptor.range = uniformSize;

        VkDescriptorImageInfo albedoDescriptor{};
        albedoDescriptor.sampler = albedoSampler;
        albedoDescriptor.imageView = albedoView;
        albedoDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkDescriptorImageInfo shadowDescriptor{};
        shadowDescriptor.sampler = shadowSampler;
        shadowDescriptor.imageView = shadowView;
        shadowDescriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        std::array<VkWriteDescriptorSet, 4> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = sets_[slot];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &albedoDescriptor;
        writes[1] = writes[0];
        writes[1].dstBinding = 1;
        writes[1].pImageInfo = &shadowDescriptor;
        writes[2] = writes[0];
        writes[2].dstBinding = 2;
        // Reflection map is filled once the reflect target exists; reuse albedo
        // until then because a null view is invalid.
        writes[2].pImageInfo = &albedoDescriptor;
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = sets_[slot];
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &bufferDescriptor;
        vkUpdateDescriptorSets(device, 4, writes.data(), 0, nullptr);
    }
    return true;
}

void SceneBindings::releaseResources(VkDevice device) noexcept {
    if (device != VK_NULL_HANDLE) {
        for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
            if (buffers_[slot] != VK_NULL_HANDLE) vkDestroyBuffer(device, buffers_[slot], nullptr);
            if (memory_[slot] != VK_NULL_HANDLE) vkFreeMemory(device, memory_[slot], nullptr);
            sets_[slot] = VK_NULL_HANDLE;
            buffers_[slot] = VK_NULL_HANDLE;
            memory_[slot] = VK_NULL_HANDLE;
        }
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }
}

void SceneBindings::destroyLayout(VkDevice device) noexcept {
    if (device != VK_NULL_HANDLE && layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, layout_, nullptr);
    }
    layout_ = VK_NULL_HANDLE;
}

bool SceneBindings::writeUniforms(std::uint32_t slot, const void* data, std::size_t size) noexcept {
    if (slot >= kSlots || memory_[slot] == VK_NULL_HANDLE || size > uniformSize_) {
        return false;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device_, memory_[slot], 0, uniformSize_, 0, &mapped) != VK_SUCCESS || mapped == nullptr) {
        return false;
    }
    std::memcpy(mapped, data, size);
    vkUnmapMemory(device_, memory_[slot]);
    return true;
}

void SceneBindings::writeImage(
        std::uint32_t binding, VkImageView view, VkSampler sampler, VkImageLayout layout) noexcept {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = sampler;
    imageInfo.imageView = view;
    imageInfo.imageLayout = layout;
    for (std::uint32_t slot = 0; slot < kSlots; ++slot) {
        if (sets_[slot] == VK_NULL_HANDLE) {
            continue;
        }
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sets_[slot];
        write.dstBinding = binding;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfo;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
}

void SceneBindings::bindAlbedo(VkImageView view, VkSampler sampler) noexcept {
    writeImage(0, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void SceneBindings::bindReflection(VkImageView view, VkSampler sampler) noexcept {
    writeImage(2, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

} // namespace Aporia::graphics_internal
