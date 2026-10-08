#include "MaterialStore.hpp"

#if defined(APORIA_EXPERIMENTAL)

#include "Device/MemoryManager/MemoryManager.hpp"
#include "Graphics/GlobalDrawer/Support/Support.hpp"
#include "Graphics/TextureManager/TextureStore.hpp"

#include <array>
#include <cstring>

namespace Aporia::graphics_internal {

bool MaterialStore::ensureLayoutAndPool(std::string& error) {
    const VkDevice device = device_.device;
    if (materialDescriptorLayout_ == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding bindings[7]{};
        for (int index = 0; index < 6; ++index) {
            bindings[index].binding = static_cast<std::uint32_t>(index);
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[index].descriptorCount = 1;
            bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        bindings[6].binding = 6;
        bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[6].descriptorCount = 1;
        bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 7;
        layoutInfo.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &materialDescriptorLayout_) != VK_SUCCESS) {
            error = "material descriptor layout creation failed";
            return false;
        }
    }

    if (materialDescriptorPool_ == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSizes[2]{};
        // One set per distinct material, and a city district has hundreds of them
        // (one per road surface plus the shared atlas material), so 64 sets ran out
        // and every later createMaterial() failed with "material set allocation
        // failed", leaving those parts on the white default texture.
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = 1024 * 6;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = 1024;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 1024;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &materialDescriptorPool_) != VK_SUCCESS) {
            error = "material descriptor pool creation failed";
            return false;
        }
    }
    return true;
}

VkDescriptorSet MaterialStore::buildSet(
    const MaterialDesc& description,
    const TextureStore& textures,
    VkBuffer& outBuffer,
    VkDeviceMemory& outMemory) {
    const VkDevice device = device_.device;
    outBuffer = VK_NULL_HANDLE;
    outMemory = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = materialDescriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &materialDescriptorLayout_;
    if (vkAllocateDescriptorSets(device, &allocateInfo, &set) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    // Per-layer UV transform UBO (binding 6).
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = 32; // 2 vec4
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    VkMemoryRequirements bufferRequirements{};
    vkGetBufferMemoryRequirements(device, outBuffer, &bufferRequirements);
    std::uint32_t memoryType = 0;
    if (!MemoryManager::findMemoryType(device_.memoryProperties, bufferRequirements.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, memoryType)) {
        return VK_NULL_HANDLE;
    }
    VkMemoryAllocateInfo bufferAllocate{};
    bufferAllocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    bufferAllocate.allocationSize = bufferRequirements.size;
    bufferAllocate.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &bufferAllocate, nullptr, &outMemory) != VK_SUCCESS ||
        vkBindBufferMemory(device, outBuffer, outMemory, 0) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device, outMemory, 0, 32, 0, &mapped) == VK_SUCCESS && mapped != nullptr) {
        float uv[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (int index = 0; index < 4; ++index) {
            uv[index] = description.uvScale[index];
            uv[4 + index] = description.uvOffset[index];
        }
        std::memcpy(mapped, uv, sizeof(uv));
        vkUnmapMemory(device, outMemory);
    }

    const auto& storeTextures = textures.resources();
    const auto defaultIt = storeTextures.find(textures.defaultHandle().value());
    const auto& fallback = defaultIt->second;
    VkDescriptorImageInfo imageInfos[6]{};
    std::array<VkWriteDescriptorSet, 7> writes{};
    for (int index = 0; index < 6; ++index) {
        const TextureHandle handle = index < 4 ? description.diffuse[index] : description.mask[index - 4];
        const auto it = storeTextures.find(handle.value());
        if (it == storeTextures.end()) {
            engineLog("[warn] material layer %d has an invalid texture handle (%llu); falling back to the white default",
                      index, static_cast<unsigned long long>(handle.value()));
        }
        const auto& resource = it != storeTextures.end() ? it->second : fallback;
        imageInfos[index].sampler = resource.sampler;
        imageInfos[index].imageView = resource.view;
        imageInfos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = set;
        writes[index].dstBinding = static_cast<std::uint32_t>(index);
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[index].descriptorCount = 1;
        writes[index].pImageInfo = &imageInfos[index];
    }
    VkDescriptorBufferInfo layerDescriptor{};
    layerDescriptor.buffer = outBuffer;
    layerDescriptor.offset = 0;
    layerDescriptor.range = 32;
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = set;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[6].descriptorCount = 1;
    writes[6].pBufferInfo = &layerDescriptor;
    vkUpdateDescriptorSets(device, 7, writes.data(), 0, nullptr);
    return set;
}

bool MaterialStore::createDefault(const TextureStore& textures, std::string& error) {
    if (!ensureLayoutAndPool(error)) {
        return false;
    }
    defaultMaterialSet_ = buildSet(MaterialDesc{}, textures, defaultMaterialBuffer_, defaultMaterialMemory_);
    if (defaultMaterialSet_ == VK_NULL_HANDLE) {
        error = "default material creation failed";
        return false;
    }
    return true;
}

MaterialHandle MaterialStore::create(const MaterialDesc& description, const TextureStore& textures, std::string& error) {
    if (!ensureLayoutAndPool(error)) {
        return invalidMaterialHandle;
    }
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkDescriptorSet set = buildSet(description, textures, buffer, memory);
    if (set == VK_NULL_HANDLE) {
        error = "material set allocation failed";
        return invalidMaterialHandle;
    }
    const std::uint64_t id = nextMaterialId_++;
    materialSets_[id] = set;
    materialBuffers_[id] = buffer;
    materialMemory_[id] = memory;
    engineLog("material created: id=%llu", static_cast<unsigned long long>(id));
    return MaterialHandle{id};
}

bool MaterialStore::update(MaterialHandle handle, const MaterialDesc& description, const TextureStore& textures) {
    if (!valid(handle)) {
        return false;
    }
    const auto it = materialSets_.find(handle.value());
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    const VkDescriptorSet set = buildSet(description, textures, buffer, memory);
    if (set == VK_NULL_HANDLE) {
        return false;
    }
    const VkDevice device = device_.device;
    if (device != VK_NULL_HANDLE) {
        // Frames may still be in flight referencing the old descriptor set.
        vkDeviceWaitIdle(device);
        vkFreeDescriptorSets(device, materialDescriptorPool_, 1, &it->second);
        if (materialBuffers_[handle.value()] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, materialBuffers_[handle.value()], nullptr);
        }
        if (materialMemory_[handle.value()] != VK_NULL_HANDLE) {
            vkFreeMemory(device, materialMemory_[handle.value()], nullptr);
        }
    }
    it->second = set;
    materialBuffers_[handle.value()] = buffer;
    materialMemory_[handle.value()] = memory;
    return true;
}

void MaterialStore::destroy(MaterialHandle handle) noexcept {
    if (!valid(handle)) {
        return;
    }
    const auto it = materialSets_.find(handle.value());
    const VkDevice device = device_.device;
    if (device != VK_NULL_HANDLE && materialDescriptorPool_ != VK_NULL_HANDLE) {
        // Frames may still be in flight referencing this descriptor set.
        vkDeviceWaitIdle(device);
        VkDescriptorSet set = it->second;
        vkFreeDescriptorSets(device, materialDescriptorPool_, 1, &set);
        if (materialBuffers_[handle.value()] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, materialBuffers_[handle.value()], nullptr);
        }
        if (materialMemory_[handle.value()] != VK_NULL_HANDLE) {
            vkFreeMemory(device, materialMemory_[handle.value()], nullptr);
        }
    }
    materialSets_.erase(it);
    materialBuffers_.erase(handle.value());
    materialMemory_.erase(handle.value());
}

bool MaterialStore::valid(MaterialHandle handle) const noexcept {
    return handle.valid() && materialSets_.count(handle.value()) != 0;
}

void MaterialStore::release(VkDevice device) noexcept {
    if (device != VK_NULL_HANDLE) {
        for (const auto& entry : materialBuffers_) {
            if (entry.second != VK_NULL_HANDLE) vkDestroyBuffer(device, entry.second, nullptr);
        }
        for (const auto& entry : materialMemory_) {
            if (entry.second != VK_NULL_HANDLE) vkFreeMemory(device, entry.second, nullptr);
        }
        if (defaultMaterialBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device, defaultMaterialBuffer_, nullptr);
        if (defaultMaterialMemory_ != VK_NULL_HANDLE) vkFreeMemory(device, defaultMaterialMemory_, nullptr);
        if (materialDescriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, materialDescriptorPool_, nullptr);
        }
        if (materialDescriptorLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, materialDescriptorLayout_, nullptr);
        }
    }
    materialDescriptorPool_ = VK_NULL_HANDLE;
    materialDescriptorLayout_ = VK_NULL_HANDLE;
    materialSets_.clear();
    materialBuffers_.clear();
    materialMemory_.clear();
    defaultMaterialSet_ = VK_NULL_HANDLE;
    defaultMaterialBuffer_ = VK_NULL_HANDLE;
    defaultMaterialMemory_ = VK_NULL_HANDLE;
}

} // namespace Aporia::graphics_internal

#endif // APORIA_EXPERIMENTAL
