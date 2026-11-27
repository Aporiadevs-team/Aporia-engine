#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// windows.h turns min/max into macros, which breaks every std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "Device/D3D12/D3D12Backend.hpp"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aporia::Backend {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t kMaxTextureSlots = 4;
constexpr std::uint32_t kMaxConstantSlots = 4;
constexpr std::uint32_t kConstantRingSize = 1024 * 1024;
constexpr std::uint32_t kSamplerCount = 4;
// Descriptor tables are GPU state: a command list keeps reading the table it
// was bound to until it is rebound. Overwriting the same slots between two
// draws of one frame would make the earlier draw sample the later texture, so
// every binding change gets its own set of descriptors. The ring is reset at
// the start of every frame. The sampler heap has a hard 2048 descriptor limit
// on feature level tier 1, which caps the ring at 512 sets of four samplers;
// the value below stays well inside that budget and still leaves room for a
// few hundred texture switches inside one frame.
constexpr std::uint32_t kDescriptorSetCount = 256;

void dumpInfoQueueMessages(ID3D12InfoQueue* queue, const char* label) {
    if (queue == nullptr) {
        return;
    }
    const UINT count = queue->GetNumStoredMessages();
    if (count == 0) {
        return;
    }
    std::printf("[d3d12] %s: %llu debug messages\n", label,
                static_cast<unsigned long long>(count));
    std::fflush(stdout);
    for (UINT index = 0; index < count; ++index) {
        std::vector<char> buffer(sizeof(D3D12_MESSAGE) + 64 * 1024);
        SIZE_T size = buffer.size();
        D3D12_MESSAGE* message = reinterpret_cast<D3D12_MESSAGE*>(buffer.data());
        message->pDescription = buffer.data() + sizeof(D3D12_MESSAGE);
        const HRESULT hr = queue->GetMessage(index, message, &size);
        std::printf("[d3d12]   [%u] hr=0x%08lX severity=%d id=%d text=%s\n", index,
                    static_cast<unsigned long>(hr), static_cast<int>(message->Severity),
                    static_cast<int>(message->ID), message->pDescription);
        std::fflush(stdout);
    }
    queue->ClearStoredMessages();
}

ID3D12InfoQueue* g_infoQueue = nullptr;

#if defined(_WIN32)
LONG WINAPI debugBreakFilter(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
        std::printf("[d3d12] debug layer breakpoint\n");
        std::fflush(stdout);
        dumpInfoQueueMessages(g_infoQueue, "breakpoint");
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Every failure message carries the HRESULT: without it a "failed to create X"
// line says nothing about the actual cause.
std::string describe(HRESULT hr, const char* what) {
    char code[16];
    std::snprintf(code, sizeof(code), "0x%08lX", static_cast<unsigned long>(hr));
    return std::string(what) + " (hr=" + code + ")";
}

DXGI_FORMAT toDxgiFormat(PixelFormat format) {
    switch (format) {
        case PixelFormat::Rgba8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::Bgra8Unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case PixelFormat::Rgba16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case PixelFormat::Rgba32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case PixelFormat::Bgra10A2Unorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case PixelFormat::R32Float: return DXGI_FORMAT_R32_FLOAT;
        case PixelFormat::Rg16Float: return DXGI_FORMAT_R16G16_FLOAT;
        case PixelFormat::Depth32Float: return DXGI_FORMAT_D32_FLOAT;
        case PixelFormat::Depth24Unorm: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case PixelFormat::Depth16Unorm: return DXGI_FORMAT_D16_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

std::size_t pixelSize(PixelFormat format) {
    switch (format) {
        case PixelFormat::Rgba8Unorm: return 4;
        case PixelFormat::Bgra8Unorm: return 4;
        case PixelFormat::Rgba16Float: return 8;
        case PixelFormat::Rgba32Float: return 16;
        case PixelFormat::Bgra10A2Unorm: return 4;
        case PixelFormat::R32Float: return 4;
        case PixelFormat::Rg16Float: return 4;
        case PixelFormat::Depth32Float: return 4;
        case PixelFormat::Depth24Unorm: return 4;
        case PixelFormat::Depth16Unorm: return 2;
    }
    return 4;
}

DXGI_FORMAT toDxgiFormat(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float32: return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float32x2: return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float32x3: return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float32x4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::Uint32: return DXGI_FORMAT_R32_UINT;
        case VertexFormat::Uint8x4Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

// Depth formats cannot be read through a shader resource view with their own
// format, so they are exposed as a plain float channel instead.
DXGI_FORMAT toShaderViewFormat(PixelFormat format) {
    if (format == PixelFormat::Depth32Float) {
        return DXGI_FORMAT_R32_FLOAT;
    }
    if (format == PixelFormat::Depth24Unorm) {
        return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    }
    if (format == PixelFormat::Depth16Unorm) {
        return DXGI_FORMAT_R16_UNORM;
    }
    return toDxgiFormat(format);
}

D3D12_RESOURCE_DESC toResourceDesc(const TextureDesc& desc) {
    D3D12_RESOURCE_DESC resource{};
    // D3D12_RESOURCE_DIMENSION_BUFFER is 1 in this SDK, so a zeroed
    // descriptor means UNKNOWN and every commit is rejected with E_INVALIDARG.
    resource.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource.Width = desc.width;
    resource.Height = desc.height;
    resource.DepthOrArraySize = desc.depth;
    resource.MipLevels = desc.mipLevels;
    resource.Format = toDxgiFormat(desc.format);
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if ((desc.usage & TextureUsage::DepthStencil) != TextureUsage{}) {
        resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    } else if ((desc.usage & TextureUsage::RenderTarget) != TextureUsage{}) {
        resource.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    } else {
        resource.Flags = D3D12_RESOURCE_FLAG_NONE;
    }
    return resource;
}

D3D12_BLEND toBlend(BlendFactor factor) {
    switch (factor) {
        case BlendFactor::Zero: return D3D12_BLEND_ZERO;
        case BlendFactor::One: return D3D12_BLEND_ONE;
        case BlendFactor::SourceColor: return D3D12_BLEND_SRC_COLOR;
        case BlendFactor::OneMinusSourceColor: return D3D12_BLEND_INV_SRC_COLOR;
        case BlendFactor::SourceAlpha: return D3D12_BLEND_SRC_ALPHA;
        case BlendFactor::OneMinusSourceAlpha: return D3D12_BLEND_INV_SRC_ALPHA;
        case BlendFactor::DestinationColor: return D3D12_BLEND_DEST_COLOR;
        case BlendFactor::OneMinusDestinationColor: return D3D12_BLEND_INV_DEST_COLOR;
        case BlendFactor::DestinationAlpha: return D3D12_BLEND_DEST_ALPHA;
        case BlendFactor::OneMinusDestinationAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
        case BlendFactor::SourceAlphaSaturate: return D3D12_BLEND_SRC_ALPHA_SAT;
    }
    return D3D12_BLEND_ONE;
}

D3D12_BLEND_OP toBlendOp(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return D3D12_BLEND_OP_ADD;
        case BlendOp::Subtract: return D3D12_BLEND_OP_SUBTRACT;
        case BlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
        case BlendOp::Min: return D3D12_BLEND_OP_MIN;
        case BlendOp::Max: return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

D3D12_COMPARISON_FUNC toCompare(CompareOp op) {
    switch (op) {
        case CompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
        case CompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
        case CompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
        case CompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case CompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
        case CompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case CompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case CompareOp::Always: return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
}

D3D12_PRIMITIVE_TOPOLOGY toTopology(Topology topology) {
    switch (topology) {
        case Topology::TriangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case Topology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case Topology::LineList: return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case Topology::Points: return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

D3D12_PRIMITIVE_TOPOLOGY_TYPE toTopologyType(Topology topology) {
    switch (topology) {
        case Topology::TriangleList:
        case Topology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case Topology::LineList: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case Topology::Points: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

const char* shaderProfile(ShaderStage stage) {
    switch (stage) {
        case ShaderStage::Vertex: return "vs_5_1";
        case ShaderStage::Fragment: return "ps_5_1";
        case ShaderStage::Geometry: return "gs_5_1";
        case ShaderStage::Compute: return "cs_5_1";
    }
    return "vs_5_1";
}

}  // namespace

struct D3D12Backend::Impl {
    struct BufferResource {
        ID3D12Resource* resource = nullptr;
        std::size_t size = 0;
    };
    struct TextureResource {
        ID3D12Resource* resource = nullptr;
        ID3D12Resource* upload = nullptr;
        TextureDesc desc;
        UINT rtvIndex = UINT_MAX;
        UINT dsvIndex = UINT_MAX;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    };
    struct ShaderResource {
        ComPtr<ID3DBlob> blob;
        ShaderStage stage = ShaderStage::Vertex;
    };
    struct PipelineResource {
        ComPtr<ID3D12PipelineState> state;
        UINT vertexStride = 0;
        D3D12_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    };

    BackendCapabilities caps;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12CommandAllocator> uploadAllocator;
    ComPtr<ID3D12GraphicsCommandList> uploadCommandList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    UINT64 fenceCompleted = 0;

    ComPtr<IDXGISwapChain3> swapChain;
    ComPtr<ID3D12DescriptorHeap> backBufferRtvHeap;
    std::vector<ID3D12Resource*> backBuffers;
    UINT backBufferIndex = 0;
    UINT32 width = 0;
    UINT32 height = 0;
    bool vsync = true;

    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    UINT rtvDescriptorSize = 0;
    UINT rtvCount = 0;
    UINT rtvCapacity = 0;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    UINT dsvDescriptorSize = 0;
    UINT dsvCount = 0;
    UINT dsvCapacity = 0;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12DescriptorHeap> samplerHeap;

    ComPtr<ID3D12Resource> constantRing;
    UINT8* constantCpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS constantBase = 0;
    std::size_t constantOffset = 0;
    struct ConstantSlot {
        D3D12_GPU_VIRTUAL_ADDRESS address = 0;
        std::size_t size = 0;
        bool set = false;
    };
    std::array<ConstantSlot, kMaxConstantSlots> constantSlots{};

    std::unordered_map<std::uint64_t, BufferResource> buffers;
    std::unordered_map<std::uint64_t, TextureResource> textures;
    std::unordered_map<std::uint64_t, ShaderResource> shaders;
    std::unordered_map<std::uint64_t, PipelineResource> pipelines;
    std::unordered_map<std::uint64_t, D3D12_SAMPLER_DESC> samplers;
    std::uint64_t nextHandle = 1;

    ComPtr<ID3D12PipelineState> currentPipeline;
    UINT currentStride = 0;
    bool descriptorTablesDirty = true;
    // Logical bindings requested by the renderer. They survive a draw: the
    // next descriptor set is filled from them, so a slot the renderer does
    // not touch keeps sampling the same texture.
    std::array<std::uint64_t, kMaxTextureSlots> boundTextures{};
    std::array<std::uint64_t, kSamplerCount> boundSamplers{};
    // What the descriptor set at descriptorSetIndex actually holds. The
    // current set is only valid while these match the logical bindings: a
    // draw binds a set, and a later binding change must go to a fresh set
    // instead of overwriting descriptors the GPU still reads.
    std::array<std::uint64_t, kMaxTextureSlots> setTextures{};
    std::array<std::uint64_t, kSamplerCount> setSamplers{};
    bool setValid = false;
    // The descriptor set the next binding change writes into.
    UINT descriptorSetIndex = 0;
    UINT srvDescriptorSize = 0;
    UINT samplerDescriptorSize = 0;
    // The fallback texture every sampler slot points at when the host has no
    // texture of its own; destroying a bound texture restores it.
    TextureHandle whiteTexture{};
    ComPtr<ID3D12InfoQueue> infoQueue;
    D3D12_RESOURCE_STATES backBufferState = D3D12_RESOURCE_STATE_PRESENT;
    // Resources destroyed between frames may still be referenced by
    // descriptor sets the GPU hasn't consumed yet. Keeping them alive
    // until the GPU is idle prevents a use-after-free that the driver
    // reports as a hung device.
    std::vector<ID3D12Resource*> pendingTextureRelease;
    std::vector<ID3D12Resource*> pendingBufferRelease;

    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle(UINT set, UINT slot) const {
        auto handle = srvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(set * kMaxTextureSlots + slot) * srvDescriptorSize;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE samplerCpuHandle(UINT set, UINT slot) const {
        auto handle = samplerHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(set * kSamplerCount + slot) * samplerDescriptorSize;
        return handle;
    }

    // Moves to a fresh set and fills it from the recorded bindings. A shader
    // visible heap cannot be a copy source, so every descriptor is rewritten
    // instead of copied.
    void allocateDescriptorSet() {
        if (!srvHeap || !samplerHeap) {
            return;
        }
        const UINT next = (descriptorSetIndex + 1) % kDescriptorSetCount;
        if (next == 0) {
            // The ring wrapped: every set may still be read by the GPU, so the
            // only safe way to reuse the first one is to wait for it.
            waitForGpu();
        }
        descriptorSetIndex = next;
        writeSrvDescriptors();
        writeSamplerDescriptors();
        // writeSrvDescriptors() may replace a destroyed texture with the
        // fallback, so the materialised copy is taken after the writes.
        setTextures = boundTextures;
        setSamplers = boundSamplers;
        setValid = true;
        descriptorTablesDirty = true;
    }

    [[nodiscard]] bool setMatchesBindings() const {
        return setValid && setTextures == boundTextures && setSamplers == boundSamplers;
    }

    void writeSrvView(UINT set, UINT slot, const TextureResource& texture) {
        D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        viewDesc.Format = toShaderViewFormat(texture.desc.format);
        viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        viewDesc.Texture2D.MipLevels = texture.desc.mipLevels;
        device->CreateShaderResourceView(texture.resource, &viewDesc, srvCpuHandle(set, slot));
    }

    // Rewrites every slot of the current set from the recorded bindings, so a
    // fresh set starts out identical to the one the GPU is still reading.
    // A slot whose texture is gone falls back to the white texture: an
    // uninitialised descriptor would make the GPU read a dangling resource.
    void writeSrvDescriptors() {
        const auto white = textures.find(whiteTexture.value);
        for (UINT slot = 0; slot < kMaxTextureSlots; ++slot) {
            const auto entry = textures.find(boundTextures[slot]);
            if (entry != textures.end()) {
                writeSrvView(descriptorSetIndex, slot, entry->second);
            } else if (white != textures.end()) {
                writeSrvView(descriptorSetIndex, slot, white->second);
                boundTextures[slot] = whiteTexture.value;
            }
        }
    }

    void writeSamplerDescriptors() {
        for (UINT slot = 0; slot < kSamplerCount; ++slot) {
            const auto entry = samplers.find(boundSamplers[slot]);
            if (entry != samplers.end()) {
                device->CreateSampler(&entry->second, samplerCpuHandle(descriptorSetIndex, slot));
            } else if (!samplers.empty()) {
                device->CreateSampler(&samplers.begin()->second,
                                      samplerCpuHandle(descriptorSetIndex, slot));
                boundSamplers[slot] = samplers.begin()->first;
            }
        }
    }

    std::uint64_t allocateHandle() { return nextHandle++; }

    void dumpInfoQueue(const char* label) { dumpInfoQueueMessages(infoQueue.Get(), label); }

    void transition(
        ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) {
        if (!list || !resource || before == after) {
            return;
        }
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        list->ResourceBarrier(1, &barrier);
    }

    // The queue has no idle wait of its own: a fence plus an event is the
    // portable way to block until the GPU caught up.
    void waitForGpu() {
        if (!queue || !fence) {
            return;
        }
        ++fenceValue;
        if (FAILED(queue->Signal(fence.Get(), fenceValue))) {
            return;
        }
        if (fence->GetCompletedValue() < fenceValue) {
            // The event is armed by the fence itself; signalling it by hand
            // would return from the wait before the GPU finished.
            if (FAILED(fence->SetEventOnCompletion(fenceValue, fenceEvent))) {
                return;
            }
            WaitForSingleObject(fenceEvent, INFINITE);
        }
        fenceCompleted = fence->GetCompletedValue();
    }

    bool ensureRtvCapacity(UINT needed);
    bool ensureDsvCapacity(UINT needed);
    bool createRootSignature(std::string& error);
    bool createDescriptorHeaps(std::string& error);
    bool createConstantRing(std::string& error);
    // Points a texture slot at the fallback white texture.
    void bindFallbackTexture(UINT slot);
};

void D3D12Backend::Impl::bindFallbackTexture(UINT slot) {
    // Only the logical binding changes here: the next draw notices that the
    // materialised set no longer matches and rebuilds it from scratch.
    if (textures.find(whiteTexture.value) == textures.end()) {
        return;
    }
    boundTextures[slot] = whiteTexture.value;
}

bool D3D12Backend::Impl::ensureRtvCapacity(UINT needed) {    if (needed <= rtvCapacity) {
        return true;
    }
    const UINT capacity = std::max(needed, rtvCapacity * 2 + 8);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = capacity;
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)))) {
        return false;
    }
    rtvHeap = heap;
    rtvCapacity = capacity;
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    rtvCount = 0;
    // The heap was replaced: rebuild the views of every texture that had one.
    for (auto& entry : textures) {
        if (entry.second.rtvIndex == UINT_MAX) {
            continue;
        }
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
        viewDesc.Format = toDxgiFormat(entry.second.desc.format);
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        auto handle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(entry.second.rtvIndex) * rtvDescriptorSize;
        device->CreateRenderTargetView(entry.second.resource, nullptr, handle);
    }
    return true;
}

bool D3D12Backend::Impl::ensureDsvCapacity(UINT needed) {
    if (needed <= dsvCapacity) {
        return true;
    }
    const UINT capacity = std::max(needed, dsvCapacity * 2 + 8);
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    heapDesc.NumDescriptors = capacity;
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)))) {
        return false;
    }
    dsvHeap = heap;
    dsvCapacity = capacity;
    dsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    dsvCount = 0;
    for (auto& entry : textures) {
        if (entry.second.dsvIndex == UINT_MAX) {
            continue;
        }
        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
        viewDesc.Format = toDxgiFormat(entry.second.desc.format);
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        auto handle = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(entry.second.dsvIndex) * dsvDescriptorSize;
        device->CreateDepthStencilView(entry.second.resource, nullptr, handle);
    }
    return true;
}

bool D3D12Backend::Impl::createRootSignature(std::string& error) {
    // One root descriptor per constant slot (b0..b3), plus descriptor tables
    // for the textures (t0..t3) and the samplers (s0..s3).
    D3D12_ROOT_PARAMETER parameters[6]{};

    D3D12_DESCRIPTOR_RANGE textureRange{};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = kMaxTextureSlots;
    textureRange.BaseShaderRegister = 0;
    textureRange.RegisterSpace = 0;
    textureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE samplerRange{};
    samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    samplerRange.NumDescriptors = kSamplerCount;
    samplerRange.BaseShaderRegister = 0;
    samplerRange.RegisterSpace = 0;
    samplerRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    for (std::uint32_t slot = 0; slot < kMaxConstantSlots; ++slot) {
        parameters[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[slot].Descriptor.ShaderRegister = slot;
        parameters[slot].Descriptor.RegisterSpace = 0;
        parameters[slot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }

    parameters[kMaxConstantSlots].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[kMaxConstantSlots].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[kMaxConstantSlots].DescriptorTable.NumDescriptorRanges = 1;
    parameters[kMaxConstantSlots].DescriptorTable.pDescriptorRanges = &textureRange;

    parameters[kMaxConstantSlots + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[kMaxConstantSlots + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[kMaxConstantSlots + 1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[kMaxConstantSlots + 1].DescriptorTable.pDescriptorRanges = &samplerRange;

    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters = 6;
    signature.pParameters = parameters;
    signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors))) {
        error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()))
                       : "D3D12SerializeRootSignature failed";
        return false;
    }
    // The device only accepts a serialized root signature blob.
    return SUCCEEDED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                 IID_PPV_ARGS(&rootSignature)));
}

bool D3D12Backend::Impl::createDescriptorHeaps(std::string& error) {
    // The sampler table and the texture table are bound to the root signature
    // by a GPU visible heap: a CPU only heap has no GPU descriptor handle.
    // Both heaps hold kDescriptorSetCount independent sets, so a binding
    // change never overwrites a table an earlier draw still reads.
    D3D12_DESCRIPTOR_HEAP_DESC samplerDesc{};
    samplerDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerDesc.NumDescriptors = kSamplerCount * kDescriptorSetCount;
    samplerDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&samplerDesc, IID_PPV_ARGS(&samplerHeap)))) {
        error = "failed to create sampler descriptor heap";
        return false;
    }
    samplerDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = kMaxTextureSlots * kDescriptorSetCount;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&srvHeap)))) {
        error = "failed to create srv descriptor heap";
        return false;
    }
    srvDescriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    if (!ensureRtvCapacity(16)) {
        error = "failed to create rtv descriptor heap";
        return false;
    }
    if (!ensureDsvCapacity(8)) {
        error = "failed to create dsv descriptor heap";
        return false;
    }
    return true;
}

bool D3D12Backend::Impl::createConstantRing(std::string& error) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = kConstantRingSize;
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.Format = DXGI_FORMAT_UNKNOWN;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource.SampleDesc.Count = 1;
    const HRESULT hr = device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&constantRing));
    if (FAILED(hr)) {
        error = describe(hr, "failed to create constant buffer ring");
        return false;
    }
    constantBase = constantRing->GetGPUVirtualAddress();
    void* mapped = nullptr;
    if (FAILED(constantRing->Map(0, nullptr, &mapped))) {
        error = "failed to map constant buffer ring";
        return false;
    }
    constantCpu = static_cast<UINT8*>(mapped);
    return true;
}

D3D12Backend::D3D12Backend() : impl_(std::make_unique<Impl>()) {
    impl_->caps.kind = BackendKind::D3D12;
    impl_->caps.apiVersion = 12;
    impl_->caps.shaderModel = 5;
    impl_->caps.computeShaders = true;
    impl_->caps.geometryShaders = true;
    impl_->caps.tessellation = true;
    impl_->caps.textureLod = true;
    impl_->caps.instancing = true;
    impl_->caps.depthStencil = true;
    impl_->caps.shadowMaps = true;
    impl_->caps.multisampling = true;
    impl_->caps.srgbOutput = true;
    impl_->caps.dynamicUniforms = true;
    impl_->caps.maxRenderTargets = 8;
    impl_->caps.maxTextureSize = 16384;
    impl_->caps.maxConstantBufferSize = 65536;
    impl_->caps.maxVertexAttributes = 16;
}

D3D12Backend::~D3D12Backend() { shutdown(); }

BackendKind D3D12Backend::kind() const { return BackendKind::D3D12; }

const BackendCapabilities& D3D12Backend::capabilities() const { return impl_->caps; }

bool D3D12Backend::initialize(const BackendConfig& config, std::string& error) {
    const bool debugLayer = std::getenv("APORIA_D3D12_DEBUG") != nullptr;
    if (debugLayer) {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
        }
    }
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&impl_->device)))) {
        error = "failed to create the D3D12 device";
        return false;
    }
    if (debugLayer) {
        impl_->device->QueryInterface(IID_PPV_ARGS(&impl_->infoQueue));
        g_infoQueue = impl_->infoQueue.Get();
        SetUnhandledExceptionFilter(debugBreakFilter);
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(impl_->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&impl_->queue)))) {
        error = "failed to create D3D12 command queue";
        return false;
    }

    if (FAILED(impl_->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&impl_->allocator)))) {
        error = "failed to create D3D12 command allocator";
        return false;
    }
    if (FAILED(impl_->device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&impl_->uploadAllocator)))) {
        error = "failed to create upload command allocator";
        return false;
    }

    if (FAILED(impl_->device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, impl_->allocator.Get(), nullptr,
            IID_PPV_ARGS(&impl_->commandList)))) {
        error = "failed to create D3D12 command list";
        return false;
    }
    impl_->commandList->Close();

    if (FAILED(impl_->device->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, impl_->uploadAllocator.Get(), nullptr,
            IID_PPV_ARGS(&impl_->uploadCommandList)))) {
        error = "failed to create upload command list";
        return false;
    }
    impl_->uploadCommandList->Close();

    if (FAILED(impl_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl_->fence)))) {
        error = "failed to create D3D12 fence";
        return false;
    }
    impl_->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (impl_->fenceEvent == nullptr) {
        error = "failed to create fence event";
        return false;
    }

    if (!impl_->createRootSignature(error)) {
        return false;
    }
    if (!impl_->createDescriptorHeaps(error)) {
        return false;
    }
    if (!impl_->createConstantRing(error)) {
        return false;
    }

    // Default white texture keeps unbound sampler slots valid.
    TextureDesc white;
    white.width = 1;
    white.height = 1;
    white.format = PixelFormat::Rgba8Unorm;
    white.usage = TextureUsage::ShaderResource;
    white.data = {255, 255, 255, 255};
    const TextureHandle whiteHandle = createTexture(white);
    impl_->whiteTexture = whiteHandle;
    for (std::uint32_t slot = 0; slot < kMaxTextureSlots; ++slot) {
        setTexture(slot, whiteHandle);
    }
    // Every sampler slot needs a valid descriptor, otherwise the draws that
    // sample a texture are skipped by the driver.
    SamplerDesc defaultSampler;
    defaultSampler.filter = FilterMode::Linear;
    defaultSampler.address = AddressMode::Clamp;
    const SamplerHandle samplerHandle = createSampler(defaultSampler);
    for (std::uint32_t slot = 0; slot < kSamplerCount; ++slot) {
        setSampler(slot, samplerHandle);
    }
    // Every ring slot the renderer may switch to has to hold a valid
    // descriptor: an uninitialised entry points at a resource that does not
    // exist and takes the GPU down on the first draw that reads it.
    for (std::uint32_t set = 0; set < kDescriptorSetCount; ++set) {
        impl_->descriptorSetIndex = set;
        impl_->writeSrvDescriptors();
        impl_->writeSamplerDescriptors();
    }
    impl_->descriptorSetIndex = 0;
    return true;
}

void D3D12Backend::shutdown() {
    if (impl_->device && impl_->queue) {
        impl_->waitForGpu();
    }
    for (ID3D12Resource* resource : impl_->pendingTextureRelease) {
        if (resource) {
            resource->Release();
        }
    }
    impl_->pendingTextureRelease.clear();
    for (ID3D12Resource* resource : impl_->pendingBufferRelease) {
        if (resource) {
            resource->Release();
        }
    }
    impl_->pendingBufferRelease.clear();
    impl_->pipelines.clear();
    impl_->shaders.clear();
    for (auto& entry : impl_->buffers) {
        if (entry.second.resource) {
            entry.second.resource->Release();
        }
    }
    impl_->buffers.clear();
    for (auto& entry : impl_->textures) {
        if (entry.second.resource) {
            entry.second.resource->Release();
        }
        if (entry.second.upload) {
            entry.second.upload->Release();
        }
    }
    impl_->textures.clear();
    impl_->samplers.clear();
    if (impl_->constantRing) {
        impl_->constantRing->Unmap(0, nullptr);
        impl_->constantRing.Reset();
    }
    impl_->constantCpu = nullptr;
    impl_->rootSignature.Reset();
    impl_->rtvHeap.Reset();
    impl_->dsvHeap.Reset();
    impl_->srvHeap.Reset();
    impl_->samplerHeap.Reset();
    impl_->backBufferRtvHeap.Reset();
    impl_->backBuffers.clear();
    impl_->swapChain.Reset();
    impl_->uploadCommandList.Reset();
    impl_->uploadAllocator.Reset();
    impl_->commandList.Reset();
    impl_->allocator.Reset();
    impl_->queue.Reset();
    impl_->fence.Reset();
    impl_->device.Reset();
    if (impl_->fenceEvent) {
        CloseHandle(impl_->fenceEvent);
        impl_->fenceEvent = nullptr;
    }
}

bool D3D12Backend::createSwapchain(const SwapchainDesc& desc, std::string& error) {
    if (desc.nativeWindow == 0) {
        error = "swapchain requires a native window";
        return false;
    }
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
        error = "CreateDXGIFactory2 failed";
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = desc.width;
    swapDesc.Height = desc.height;
    swapDesc.Format = toDxgiFormat(desc.format);
    swapDesc.BufferCount = 2;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    const HRESULT swapHr = factory->CreateSwapChainForHwnd(
        impl_->queue.Get(), reinterpret_cast<HWND>(desc.nativeWindow), &swapDesc, nullptr, nullptr,
        &swapChain);
    if (FAILED(swapHr)) {
        const HRESULT removed = impl_->device->GetDeviceRemovedReason();
        error = describe(swapHr, "CreateSwapChainForHwnd failed") + " " + describe(removed, "removed");
        return false;
    }
    factory->MakeWindowAssociation(reinterpret_cast<HWND>(desc.nativeWindow), DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(swapChain.As(&impl_->swapChain))) {
        error = "IDXGISwapChain3 is required";
        return false;
    }

    impl_->width = desc.width;
    impl_->height = desc.height;
    impl_->vsync = desc.vsync;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    DXGI_SWAP_CHAIN_DESC chainDesc{};
    impl_->swapChain->GetDesc(&chainDesc);
    const UINT bufferCount = chainDesc.BufferCount;
    heapDesc.NumDescriptors = bufferCount;
    if (FAILED(impl_->device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&impl_->backBufferRtvHeap)))) {
        error = "failed to create back buffer descriptor heap";
        return false;
    }

    const UINT rtvSize = impl_->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    impl_->backBuffers.resize(bufferCount);
    for (UINT index = 0; index < bufferCount; ++index) {
        if (FAILED(impl_->swapChain->GetBuffer(index, IID_PPV_ARGS(&impl_->backBuffers[index])))) {
            error = "failed to fetch swapchain buffer";
            return false;
        }
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
        viewDesc.Format = toDxgiFormat(desc.format);
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        auto handle = impl_->backBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * rtvSize;
        impl_->device->CreateRenderTargetView(impl_->backBuffers[index], nullptr, handle);
    }
    return true;
}

void D3D12Backend::resizeSwapchain(std::uint32_t width, std::uint32_t height) {
    if (!impl_->swapChain || width == 0 || height == 0) {
        return;
    }
    impl_->waitForGpu();
    // Release old back buffer pointers (the resources themselves are held by
    // the swap chain and will be released when the swap chain is resized).
    for (auto* buf : impl_->backBuffers) {
        if (buf) buf->Release();
    }
    impl_->backBuffers.clear();
    const HRESULT resizeHr = impl_->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(resizeHr)) {
        return;
    }
    impl_->width = width;
    impl_->height = height;
    impl_->backBufferState = D3D12_RESOURCE_STATE_PRESENT;

    const UINT rtvSize = impl_->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    DXGI_SWAP_CHAIN_DESC chainDesc{};
    impl_->swapChain->GetDesc(&chainDesc);
    const UINT bufferCount = chainDesc.BufferCount;
    impl_->backBuffers.resize(bufferCount);
    for (UINT index = 0; index < bufferCount; ++index) {
        impl_->swapChain->GetBuffer(index, IID_PPV_ARGS(&impl_->backBuffers[index]));
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
        viewDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        auto handle = impl_->backBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * rtvSize;
        impl_->device->CreateRenderTargetView(impl_->backBuffers[index], nullptr, handle);
    }
}

void D3D12Backend::setVsync(bool enabled) {
    impl_->vsync = enabled;
}

bool D3D12Backend::beginFrame(std::string& error) {
    if (!impl_->commandList) {
        error = "backend is not initialized";
        return false;
    }
    if (impl_->fenceValue > impl_->fenceCompleted) {
        // The event has to be armed by the fence itself: a manual signal
        // would release the wait before the GPU reached the fence.
        if (FAILED(impl_->fence->SetEventOnCompletion(impl_->fenceValue, impl_->fenceEvent))) {
            error = "failed to arm the fence event";
            return false;
        }
        if (WaitForSingleObject(impl_->fenceEvent, 2000) == WAIT_TIMEOUT) {
            error = "timed out waiting for the GPU";
            return false;
        }
        impl_->fenceCompleted = impl_->fence->GetCompletedValue();
    }

    // The GPU finished every command submitted before this frame, so the
    // resources released since the last frame can go now. Releasing them any
    // earlier would leave a descriptor set reading freed memory.
    for (ID3D12Resource* resource : impl_->pendingTextureRelease) {
        if (resource) {
            resource->Release();
        }
    }
    impl_->pendingTextureRelease.clear();
    for (ID3D12Resource* resource : impl_->pendingBufferRelease) {
        if (resource) {
            resource->Release();
        }
    }
    impl_->pendingBufferRelease.clear();

    if (FAILED(impl_->allocator->Reset())) {
        error = "failed to reset command allocator";
        return false;
    }
    if (FAILED(impl_->commandList->Reset(impl_->allocator.Get(), nullptr))) {
        error = "failed to reset command list";
        return false;
    }
    impl_->constantOffset = 0;
    for (auto& slot : impl_->constantSlots) {
        slot.set = false;
    }
    impl_->currentPipeline.Reset();
    impl_->currentStride = 0;
    impl_->descriptorTablesDirty = true;
    // The previous frame finished before this one started, so the descriptor
    // ring can start over from its first set. The set itself is stale: the
    // first draw of the frame has to materialise the bindings again.
    impl_->descriptorSetIndex = 0;
    impl_->setValid = false;
    // The descriptor heaps have to be set on the command list before any
    // descriptor table is bound to the root signature.
    ID3D12DescriptorHeap* heaps[2] = {impl_->srvHeap.Get(), impl_->samplerHeap.Get()};
    impl_->commandList->SetDescriptorHeaps(2, heaps);
    return true;
}

void D3D12Backend::beginRenderPass(const RenderPassDesc& desc) {
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    rtvHandles.reserve(desc.colorTargets.size());
    for (const RenderTargetBinding& target : desc.colorTargets) {
        if (target.source == RenderTargetBinding::Source::Swapchain) {
            auto handle = impl_->backBufferRtvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(impl_->backBufferIndex) *
                          impl_->device->GetDescriptorHandleIncrementSize(
                              D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            rtvHandles.push_back(handle);
            impl_->transition(impl_->commandList.Get(), impl_->backBuffers[impl_->backBufferIndex],
                              impl_->backBufferState, D3D12_RESOURCE_STATE_RENDER_TARGET);
            impl_->backBufferState = D3D12_RESOURCE_STATE_RENDER_TARGET;
            continue;
        }
        const auto entry = impl_->textures.find(target.texture.value);
        if (entry == impl_->textures.end() || entry->second.rtvIndex == UINT_MAX) {
            rtvHandles.push_back(D3D12_CPU_DESCRIPTOR_HANDLE{0});
            continue;
        }
        auto handle = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(entry->second.rtvIndex) * impl_->rtvDescriptorSize;
        rtvHandles.push_back(handle);
        impl_->transition(impl_->commandList.Get(), entry->second.resource, entry->second.state,
                          D3D12_RESOURCE_STATE_RENDER_TARGET);
        entry->second.state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE depthHandle{0};
    if (desc.hasDepth) {
        const auto entry = impl_->textures.find(desc.depthTarget.texture.value);
        if (entry != impl_->textures.end() && entry->second.dsvIndex != UINT_MAX) {
            auto handle = impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(entry->second.dsvIndex) * impl_->dsvDescriptorSize;
            depthHandle = handle;
            impl_->transition(impl_->commandList.Get(), entry->second.resource, entry->second.state,
                              D3D12_RESOURCE_STATE_DEPTH_WRITE);
            entry->second.state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
    }

    // A null depth handle has to be passed as a null pointer: the runtime
    // dereferences the descriptor pointer it is given.
    const D3D12_CPU_DESCRIPTOR_HANDLE* depthDescriptor =
        depthHandle.ptr != 0 ? &depthHandle : nullptr;
    if (!rtvHandles.empty()) {
        impl_->commandList->OMSetRenderTargets(
            static_cast<UINT>(rtvHandles.size()), rtvHandles.data(), FALSE, depthDescriptor);
    } else {
        impl_->commandList->OMSetRenderTargets(0, nullptr, FALSE, depthDescriptor);
    }

    if (desc.clearColorEnabled) {
        for (const D3D12_CPU_DESCRIPTOR_HANDLE& handle : rtvHandles) {
            impl_->commandList->ClearRenderTargetView(handle, desc.clear.color, 0, nullptr);
        }
    }
    if (desc.hasDepth && depthHandle.ptr != 0) {
        impl_->commandList->ClearDepthStencilView(
            depthHandle, D3D12_CLEAR_FLAG_DEPTH, desc.clear.depth, 0.0f, 0, nullptr);
    }
}

void D3D12Backend::endRenderPass() {}

void D3D12Backend::waitIdle() {
    if (impl_->device && impl_->queue) {
        impl_->waitForGpu();
    }
}

bool D3D12Backend::endFrame(std::string& error) {
    // The back buffer has to be in PRESENT state when Present() is called.
    if (!impl_->backBuffers.empty()) {
        impl_->transition(impl_->commandList.Get(), impl_->backBuffers[impl_->backBufferIndex],
                          impl_->backBufferState, D3D12_RESOURCE_STATE_PRESENT);
        impl_->backBufferState = D3D12_RESOURCE_STATE_PRESENT;
    }
    if (FAILED(impl_->commandList->Close())) {
        error = "failed to close command list";
        return false;
    }
    ID3D12CommandList* lists[] = {impl_->commandList.Get()};
    impl_->queue->ExecuteCommandLists(1, lists);

    const UINT presentFlags = impl_->vsync ? 0 : DXGI_PRESENT_DO_NOT_WAIT;
    if (impl_->swapChain) {
        const HRESULT presentResult = impl_->swapChain->Present(0, presentFlags);
        if (presentResult == DXGI_ERROR_DEVICE_REMOVED || presentResult == DXGI_ERROR_DEVICE_RESET) {
            error = "the D3D12 device was removed";
            return false;
        }
    }

    ++impl_->fenceValue;
    if (FAILED(impl_->queue->Signal(impl_->fence.Get(), impl_->fenceValue))) {
        error = "failed to signal fence";
        return false;
    }

    if (impl_->swapChain) {
        impl_->backBufferIndex = impl_->swapChain->GetCurrentBackBufferIndex();
    }
    impl_->dumpInfoQueue("endFrame");
    return true;
}

void D3D12Backend::setPipeline(PipelineHandle pipeline) {
    const auto entry = impl_->pipelines.find(pipeline.value);
    if (entry == impl_->pipelines.end()) {
        return;
    }
    impl_->currentPipeline = entry->second.state;
    impl_->currentStride = entry->second.vertexStride;
    impl_->commandList->SetPipelineState(impl_->currentPipeline.Get());
    impl_->commandList->SetGraphicsRootSignature(impl_->rootSignature.Get());
    // The primitive topology lives in the command list, not in the pipeline
    // state: without it the draw is discarded as an undefined topology.
    impl_->commandList->IASetPrimitiveTopology(entry->second.topology);
}

void D3D12Backend::setViewport(const Viewport& viewport) {
    const D3D12_VIEWPORT view{viewport.x, viewport.y, viewport.width, viewport.height,
                               viewport.minDepth, viewport.maxDepth};
    impl_->commandList->RSSetViewports(1, &view);

    // Direct3D always runs the scissor test and the rectangle starts out
    // empty, so nothing would reach the target without setting it. The
    // viewport is the natural default; an explicit setScissor() call still
    // overrides it.
    const LONG left = static_cast<LONG>(viewport.x);
    const LONG top = static_cast<LONG>(viewport.y);
    const LONG right = static_cast<LONG>(viewport.x + viewport.width);
    const LONG bottom = static_cast<LONG>(viewport.y + viewport.height);
    const D3D12_RECT rect{std::max<LONG>(left, 0), std::max<LONG>(top, 0),
                          std::max<LONG>(right, 0), std::max<LONG>(bottom, 0)};
    impl_->commandList->RSSetScissorRects(1, &rect);
}

void D3D12Backend::setScissor(const ScissorRect& scissor) {
    const D3D12_RECT rect{scissor.x, scissor.y, scissor.x + scissor.width, scissor.y + scissor.height};
    impl_->commandList->RSSetScissorRects(1, &rect);
}

void D3D12Backend::bindVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::uint32_t offset) {
    const auto entry = impl_->buffers.find(buffer.value);
    if (entry == impl_->buffers.end() || offset >= entry->second.size) {
        return;
    }
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = entry->second.resource->GetGPUVirtualAddress() + offset;
    view.SizeInBytes = static_cast<UINT>(entry->second.size - offset);
    view.StrideInBytes = impl_->currentStride;
    impl_->commandList->IASetVertexBuffers(slot, 1, &view);
}

void D3D12Backend::bindIndexBuffer(BufferHandle buffer, IndexFormat format) {
    const auto entry = impl_->buffers.find(buffer.value);
    if (entry == impl_->buffers.end()) {
        return;
    }
    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = entry->second.resource->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(entry->second.size);
    view.Format = format == IndexFormat::Uint32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
    impl_->commandList->IASetIndexBuffer(&view);
}

void D3D12Backend::setConstantBuffer(std::uint32_t slot, const void* data, std::size_t size) {
    if (slot >= kMaxConstantSlots || !impl_->constantCpu || size == 0) {
        return;
    }
    const std::size_t aligned = (size + 255) & ~static_cast<std::size_t>(255);
    if (impl_->constantOffset + aligned > kConstantRingSize) {
        impl_->constantOffset = 0;
    }
    std::memcpy(impl_->constantCpu + impl_->constantOffset, data, size);
    impl_->constantSlots[slot].address = impl_->constantBase + impl_->constantOffset;
    impl_->constantSlots[slot].size = size;
    impl_->constantSlots[slot].set = true;
    impl_->constantOffset += aligned;
}

void D3D12Backend::setTexture(std::uint32_t slot, TextureHandle texture) {
    if (slot >= kMaxTextureSlots) {
        return;
    }
    const auto entry = impl_->textures.find(texture.value);
    if (entry == impl_->textures.end()) {
        return;
    }
    // A texture that is still bound as a render target (a depth map, for
    // example) has to leave that state before it can be sampled. The engine
    // samples textures from the pixel shader, so PIXEL_SHADER_RESOURCE is the
    // state that matches: NON_PIXEL does not grant the pixel stage access and
    // leaves the read undefined.
    //
    // The transition runs before the "already bound" early-out below: a
    // texture can go back to being a render target between two frames (the
    // shadow map does exactly that), so the sampled state has to be restored
    // even when the handle in the slot did not change.
    const D3D12_RESOURCE_STATES current = entry->second.state;
    if (current != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE &&
        current != D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE &&
        current != D3D12_RESOURCE_STATE_COMMON) {
        impl_->transition(impl_->commandList.Get(), entry->second.resource, current,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        entry->second.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    // Re-binding the texture a slot already holds is free. The check runs
    // against the logical bindings: if the current descriptor set predates
    // the change, the next draw rebuilds it anyway.
    if (impl_->boundTextures[slot] == texture.value) {
        return;
    }
    // The view is not written here. A descriptor set the GPU may still read
    // must not be overwritten, so the change is recorded and materialised
    // into a fresh set by the next draw.
    impl_->boundTextures[slot] = texture.value;
}

void D3D12Backend::setSampler(std::uint32_t slot, SamplerHandle sampler) {
    if (slot >= kSamplerCount) {
        return;
    }
    const auto entry = impl_->samplers.find(sampler.value);
    if (entry == impl_->samplers.end()) {
        return;
    }
    if (impl_->boundSamplers[slot] == sampler.value) {
        return;
    }
    // Like textures: the descriptor is written when a draw materialises the
    // set, never into a set the GPU may still read.
    impl_->boundSamplers[slot] = sampler.value;
}

void D3D12Backend::draw(std::uint32_t vertexCount, std::uint32_t instanceCount) {
    bindDescriptorTables();
    impl_->commandList->DrawInstanced(vertexCount, instanceCount, 0, 0);
}

void D3D12Backend::drawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount) {
    bindDescriptorTables();
    impl_->commandList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);
}

void D3D12Backend::bindDescriptorTables() {
    // The set is rebuilt whenever the logical bindings moved away from what
    // it holds. Reusing a set that still matches is what keeps the common
    // "same texture, new object constants" draw cheap.
    if (!impl_->setMatchesBindings()) {
        impl_->allocateDescriptorSet();
    }
    if (impl_->descriptorTablesDirty && impl_->srvHeap && impl_->samplerHeap) {
        auto srvTable = impl_->srvHeap->GetGPUDescriptorHandleForHeapStart();
        srvTable.ptr += static_cast<SIZE_T>(impl_->descriptorSetIndex * kMaxTextureSlots) *
                        impl_->srvDescriptorSize;
        impl_->commandList->SetGraphicsRootDescriptorTable(kMaxConstantSlots, srvTable);
        auto samplerTable = impl_->samplerHeap->GetGPUDescriptorHandleForHeapStart();
        samplerTable.ptr += static_cast<SIZE_T>(impl_->descriptorSetIndex * kSamplerCount) *
                            impl_->samplerDescriptorSize;
        impl_->commandList->SetGraphicsRootDescriptorTable(kMaxConstantSlots + 1, samplerTable);
        impl_->descriptorTablesDirty = false;
    }
    for (std::uint32_t slot = 0; slot < kMaxConstantSlots; ++slot) {
        if (impl_->constantSlots[slot].set) {
            impl_->commandList->SetGraphicsRootConstantBufferView(slot, impl_->constantSlots[slot].address);
        }
    }
}

BufferHandle D3D12Backend::createBuffer(const BufferDesc& desc) {
    if (desc.size == 0) {
        return BufferHandle{};
    }
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = desc.size;
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.Format = DXGI_FORMAT_UNKNOWN;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resource.SampleDesc.Count = 1;

    Impl::BufferResource buffer;
    if (FAILED(impl_->device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &resource, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&buffer.resource)))) {
        return BufferHandle{};
    }
    if (!desc.data.empty()) {
        void* mapped = nullptr;
        if (SUCCEEDED(buffer.resource->Map(0, nullptr, &mapped))) {
            std::memcpy(mapped, desc.data.data(), std::min(desc.data.size(), desc.size));
            buffer.resource->Unmap(0, nullptr);
        }
    }
    buffer.size = desc.size;
    const std::uint64_t handle = impl_->allocateHandle();
    impl_->buffers.emplace(handle, buffer);
    return BufferHandle{handle};
}

void D3D12Backend::destroyBuffer(BufferHandle buffer) {
    const auto entry = impl_->buffers.find(buffer.value);
    if (entry == impl_->buffers.end()) {
        return;
    }
    // A command list of an earlier frame may still read this buffer, so the
    // release waits until the GPU is done with it.
    if (entry->second.resource) {
        impl_->pendingBufferRelease.push_back(entry->second.resource);
    }
    impl_->buffers.erase(entry);
}

bool D3D12Backend::updateBuffer(BufferHandle buffer, const void* data, std::size_t size) {
    const auto entry = impl_->buffers.find(buffer.value);
    if (entry == impl_->buffers.end() || data == nullptr || size == 0 || size > entry->second.size) {
        return false;
    }
    void* mapped = nullptr;
    if (FAILED(entry->second.resource->Map(0, nullptr, &mapped))) {
        return false;
    }
    std::memcpy(mapped, data, size);
    entry->second.resource->Unmap(0, nullptr);
    return true;
}

TextureHandle D3D12Backend::createTexture(const TextureDesc& desc) {
    if (desc.width == 0 || desc.height == 0) {
        return TextureHandle{};
    }
    const D3D12_RESOURCE_DESC resourceDesc = toResourceDesc(desc);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    Impl::TextureResource texture;
    texture.desc = desc;
    // A clear value at creation lets the driver use a fast clear, but it is
    // only allowed for render targets and depth buffers.
    D3D12_CLEAR_VALUE clearValue{};
    const D3D12_CLEAR_VALUE* optimizedClear = nullptr;
    if ((desc.usage & TextureUsage::DepthStencil) != TextureUsage{}) {
        clearValue.Format = resourceDesc.Format;
        clearValue.DepthStencil.Depth = 1.0f;
        clearValue.DepthStencil.Stencil = 0;
        optimizedClear = &clearValue;
    } else if ((desc.usage & TextureUsage::RenderTarget) != TextureUsage{}) {
        clearValue.Format = resourceDesc.Format;
        optimizedClear = &clearValue;
    }
    const HRESULT createHr = impl_->device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, optimizedClear,
        IID_PPV_ARGS(&texture.resource));
    if (FAILED(createHr)) {
        return TextureHandle{};
    }

    if (!desc.data.empty()) {
        const std::size_t bytesPerPixel = pixelSize(desc.format);
        const std::size_t tightPitch = static_cast<std::size_t>(desc.width) * bytesPerPixel;
        // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT: a footprint row pitch below 256
        // bytes makes the copy invalid and takes the device down.
        const std::size_t rowPitch = (tightPitch + 255) & ~std::size_t(255);
        const std::size_t total = rowPitch * desc.height;
        D3D12_HEAP_PROPERTIES uploadHeap{};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC uploadDesc{};
        uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        uploadDesc.Width = total;
        uploadDesc.Height = 1;
        uploadDesc.DepthOrArraySize = 1;
        uploadDesc.MipLevels = 1;
        uploadDesc.Format = DXGI_FORMAT_UNKNOWN;
        uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        uploadDesc.SampleDesc.Count = 1;
        if (SUCCEEDED(impl_->device->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&texture.upload)))) {
            void* mapped = nullptr;
            if (SUCCEEDED(texture.upload->Map(0, nullptr, &mapped))) {
                auto* destination = static_cast<std::uint8_t*>(mapped);
                for (std::uint32_t row = 0; row < desc.height; ++row) {
                    const std::size_t offset = static_cast<std::size_t>(row) * tightPitch;
                    if (offset >= desc.data.size()) {
                        break;
                    }
                    const std::size_t count = std::min(tightPitch, desc.data.size() - offset);
                    std::memcpy(destination + row * rowPitch, desc.data.data() + offset, count);
                }
                texture.upload->Unmap(0, nullptr);
            }
            // Upload through a dedicated command list so an in-flight frame is untouched.
            impl_->uploadAllocator->Reset();
            impl_->uploadCommandList->Reset(impl_->uploadAllocator.Get(), nullptr);
            D3D12_TEXTURE_COPY_LOCATION destination{};
            destination.pResource = texture.resource;
            destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            destination.SubresourceIndex = 0;
            D3D12_TEXTURE_COPY_LOCATION source{};
            source.pResource = texture.upload;
            source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            source.PlacedFootprint.Offset = 0;
            source.PlacedFootprint.Footprint.Format = toDxgiFormat(desc.format);
            source.PlacedFootprint.Footprint.Width = desc.width;
            source.PlacedFootprint.Footprint.Height = desc.height;
            source.PlacedFootprint.Footprint.Depth = 1;
            source.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);
            const D3D12_BOX box{0, 0, 0, desc.width, desc.height, 1};
            impl_->transition(impl_->uploadCommandList.Get(), texture.resource,
                              D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
            impl_->uploadCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
            impl_->transition(impl_->uploadCommandList.Get(), texture.resource,
                              D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
            impl_->uploadCommandList->Close();
            ID3D12CommandList* lists[] = {impl_->uploadCommandList.Get()};
            impl_->queue->ExecuteCommandLists(1, lists);
            impl_->waitForGpu();
        }
    }

    if ((desc.usage & TextureUsage::RenderTarget) != TextureUsage{}) {
        if (!impl_->ensureRtvCapacity(impl_->rtvCount + 1)) {
            texture.resource->Release();
            return TextureHandle{};
        }
        texture.rtvIndex = impl_->rtvCount++;
        D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
        viewDesc.Format = toDxgiFormat(desc.format);
        viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        auto handle = impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(texture.rtvIndex) * impl_->rtvDescriptorSize;
        impl_->device->CreateRenderTargetView(texture.resource, &viewDesc, handle);
    }

    if ((desc.usage & TextureUsage::DepthStencil) != TextureUsage{}) {
        if (!impl_->ensureDsvCapacity(impl_->dsvCount + 1)) {
            texture.resource->Release();
            return TextureHandle{};
        }
        texture.dsvIndex = impl_->dsvCount++;
        D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
        viewDesc.Format = toDxgiFormat(desc.format);
        viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        auto handle = impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(texture.dsvIndex) * impl_->dsvDescriptorSize;
        impl_->device->CreateDepthStencilView(texture.resource, &viewDesc, handle);
    }

    const std::uint64_t handle = impl_->allocateHandle();
    impl_->textures.emplace(handle, std::move(texture));
    return TextureHandle{handle};
}

void D3D12Backend::destroyTexture(TextureHandle texture) {
    const auto entry = impl_->textures.find(texture.value);
    if (entry == impl_->textures.end()) {
        return;
    }
    // The resource goes into the deferred list: descriptor sets of earlier
    // frames may still point at it until the GPU catches up.
    if (entry->second.resource) {
        impl_->pendingTextureRelease.push_back(entry->second.resource);
    }
    if (entry->second.upload) {
        impl_->pendingTextureRelease.push_back(entry->second.upload);
    }
    impl_->textures.erase(entry);
    // A slot that still points at the destroyed texture would sample a
    // dangling descriptor, so it falls back to the white texture.
    if (texture.value == impl_->whiteTexture.value) {
        return;
    }
    for (std::uint32_t slot = 0; slot < kMaxTextureSlots; ++slot) {
        if (impl_->boundTextures[slot] == texture.value) {
            impl_->bindFallbackTexture(slot);
        }
    }
}

SamplerHandle D3D12Backend::createSampler(const SamplerDesc& desc) {
    D3D12_SAMPLER_DESC sampler{};
    sampler.Filter = desc.filter == FilterMode::Nearest ? D3D12_FILTER_MIN_MAG_MIP_POINT
                                                        : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    const D3D12_TEXTURE_ADDRESS_MODE mode = desc.address == AddressMode::Wrap
                                                 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                             : desc.address == AddressMode::Mirror
                                                 ? D3D12_TEXTURE_ADDRESS_MODE_MIRROR
                                                 : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressU = mode;
    sampler.AddressV = mode;
    sampler.AddressW = mode;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.BorderColor[0] = 0.0f;
    sampler.BorderColor[1] = 0.0f;
    sampler.BorderColor[2] = 0.0f;
    sampler.BorderColor[3] = 0.0f;

    const std::uint64_t handle = impl_->allocateHandle();
    impl_->samplers.emplace(handle, sampler);
    return SamplerHandle{handle};
}

void D3D12Backend::destroySampler(SamplerHandle sampler) { impl_->samplers.erase(sampler.value); }

ShaderHandle D3D12Backend::createShader(const ShaderDesc& desc, std::string& error) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errors;
    const UINT flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR;
    if (FAILED(D3DCompile(
            desc.source.c_str(), desc.source.size(), nullptr, nullptr, nullptr, desc.entryPoint.c_str(),
            shaderProfile(desc.stage), flags, 0, &blob, &errors))) {
        error = errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()))
                       : "D3DCompile failed";
        return ShaderHandle{};
    }

    Impl::ShaderResource shader;
    shader.blob = blob;
    shader.stage = desc.stage;
    const std::uint64_t handle = impl_->allocateHandle();
    impl_->shaders.emplace(handle, shader);
    return ShaderHandle{handle};
}

void D3D12Backend::destroyShader(ShaderHandle shader) { impl_->shaders.erase(shader.value); }

PipelineHandle D3D12Backend::createPipeline(const PipelineDesc& desc, std::string& error) {
    // A pipeline without a fragment shader is a depth-only pass: the rasterizer
    // still runs, but nothing is written to a colour target.
    const bool depthOnly = desc.fragmentShader.value == 0;
    const auto vertexEntry = impl_->shaders.find(desc.vertexShader.value);
    const auto fragmentEntry = impl_->shaders.find(desc.fragmentShader.value);
    if (vertexEntry == impl_->shaders.end() || vertexEntry->second.stage != ShaderStage::Vertex ||
        (!depthOnly && (fragmentEntry == impl_->shaders.end() ||
                        fragmentEntry->second.stage != ShaderStage::Fragment))) {
        error = "pipeline is missing a compiled shader";
        return PipelineHandle{};
    }

    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    elements.reserve(desc.attributes.size());
    for (const VertexAttribute& attribute : desc.attributes) {
        D3D12_INPUT_ELEMENT_DESC element{};
        element.SemanticName = "ATTRIBUTE";
        element.SemanticIndex = attribute.slot;
        element.Format = toDxgiFormat(attribute.format);
        element.InputSlot = 0;
        element.AlignedByteOffset = attribute.offset;
        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        elements.push_back(element);
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature = impl_->rootSignature.Get();
    pso.VS = {vertexEntry->second.blob->GetBufferPointer(), vertexEntry->second.blob->GetBufferSize()};
    if (!depthOnly) {
        pso.PS = {fragmentEntry->second.blob->GetBufferPointer(), fragmentEntry->second.blob->GetBufferSize()};
    }
    pso.InputLayout = {elements.empty() ? nullptr : elements.data(), static_cast<UINT>(elements.size())};
    pso.PrimitiveTopologyType = toTopologyType(desc.topology);
    pso.SampleMask = UINT_MAX;
    pso.SampleDesc.Count = 1;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = desc.cull == CullMode::None   ? D3D12_CULL_MODE_NONE
                                  : desc.cull == CullMode::Front ? D3D12_CULL_MODE_FRONT
                                                                 : D3D12_CULL_MODE_BACK;
    // Every mesh the engine generates winds its triangles counter-clockwise
    // when seen from the front, which is the OpenGL convention. Direct3D
    // defaults to clockwise, so without this flag the rasterizer would drop
    // the front faces and keep the inside of the geometry.
    pso.RasterizerState.FrontCounterClockwise = TRUE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.DepthStencilState.DepthEnable = desc.depthTest ? TRUE : FALSE;
    pso.DepthStencilState.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL
                                                           : D3D12_DEPTH_WRITE_MASK_ZERO;
    pso.DepthStencilState.DepthFunc = toCompare(desc.depthCompare);
    pso.DepthStencilState.StencilEnable = FALSE;
    if (desc.hasDepth) {
        pso.DSVFormat = toDxgiFormat(desc.depthFormat);
    }

    std::vector<DXGI_FORMAT> formats;
    formats.reserve(desc.renderTargets.size());
    if (!depthOnly) {
        for (const RenderTargetDesc& target : desc.renderTargets) {
            formats.push_back(toDxgiFormat(target.format));
        }
    }
    pso.NumRenderTargets = static_cast<UINT>(formats.size());
    for (std::size_t index = 0; index < formats.size() && index < 8; ++index) {
        pso.RTVFormats[index] = formats[index];
    }

    for (std::size_t index = 0; index < desc.renderTargets.size() && !depthOnly; ++index) {
        const RenderTargetDesc& target = desc.renderTargets[index];
        D3D12_RENDER_TARGET_BLEND_DESC& blend = pso.BlendState.RenderTarget[index];
        blend.BlendEnable = target.blendEnable ? TRUE : FALSE;
        blend.SrcBlend = toBlend(target.srcColor);
        blend.DestBlend = toBlend(target.dstColor);
        blend.BlendOp = toBlendOp(target.blendOp);
        blend.SrcBlendAlpha = toBlend(target.srcAlpha);
        blend.DestBlendAlpha = toBlend(target.dstAlpha);
        blend.BlendOpAlpha = toBlendOp(target.blendOp);
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = target.writeAlpha
            ? D3D12_COLOR_WRITE_ENABLE_ALL
            : (D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
               D3D12_COLOR_WRITE_ENABLE_BLUE);
    }

    ComPtr<ID3D12PipelineState> state;
    if (FAILED(impl_->device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&state)))) {
        error = "CreateGraphicsPipelineState failed";
        return PipelineHandle{};
    }
    const std::uint64_t handle = impl_->allocateHandle();
    impl_->pipelines.emplace(handle, Impl::PipelineResource{state, desc.vertexStride, toTopology(desc.topology)});
    return PipelineHandle{handle};
}

void D3D12Backend::destroyPipeline(PipelineHandle pipeline) { impl_->pipelines.erase(pipeline.value); }

bool D3D12Backend::readPixels(
    TextureHandle texture, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    std::vector<std::uint8_t>& out) {
    const auto entry = impl_->textures.find(texture.value);
    if (entry == impl_->textures.end() || width == 0 || height == 0) {
        std::printf("[dbg] readPixels: no texture\n"); std::fflush(stdout);
        return false;
    }
    const TextureDesc& desc = entry->second.desc;

    // Readback goes through a buffer with a 256 byte aligned row pitch: a
    // texture in a readback heap would have an implicit row layout the
    // footprint cannot describe.
    const std::size_t bytesPerPixel = pixelSize(desc.format);
    const std::size_t tightPitch = static_cast<std::size_t>(width) * bytesPerPixel;
    const std::size_t rowPitch = (tightPitch + 255) & ~std::size_t(255);

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC staging{};
    staging.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    staging.Width = rowPitch * height;
    staging.Height = 1;
    staging.DepthOrArraySize = 1;
    staging.MipLevels = 1;
    staging.Format = DXGI_FORMAT_UNKNOWN;
    staging.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    staging.SampleDesc.Count = 1;

    ComPtr<ID3D12Resource> stagingBuffer;
    const HRESULT stagingHr = impl_->device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &staging, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&stagingBuffer));
    if (FAILED(stagingHr)) {
        std::printf("[dbg] readPixels: staging create hr=0x%08lX w=%llu removed=0x%08lX\n",
                    (unsigned long)stagingHr, (unsigned long long)staging.Width,
                    (unsigned long)impl_->device->GetDeviceRemovedReason());
        std::fflush(stdout);
        return false;
    }

    const HRESULT allocHr = impl_->uploadAllocator->Reset();
    const HRESULT listHr = impl_->uploadCommandList->Reset(impl_->uploadAllocator.Get(), nullptr);
    std::printf("[dbg] readPixels: alloc=0x%08lX list=0x%08lX state=%d\n",
        (unsigned long)allocHr,(unsigned long)listHr,(int)entry->second.state); std::fflush(stdout);
    impl_->transition(impl_->uploadCommandList.Get(), entry->second.resource, entry->second.state,
                      D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = stagingBuffer.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Offset = 0;
    destination.PlacedFootprint.Footprint.Format = toDxgiFormat(desc.format);
    destination.PlacedFootprint.Footprint.Width = width;
    destination.PlacedFootprint.Footprint.Height = height;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = entry->second.resource;
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = 0;

    const D3D12_BOX box{x, y, 0, x + width, y + height, 1};
    impl_->uploadCommandList->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
    impl_->transition(impl_->uploadCommandList.Get(), entry->second.resource,
                      D3D12_RESOURCE_STATE_COPY_SOURCE, entry->second.state);
    impl_->uploadCommandList->Close();
    ID3D12CommandList* lists[] = {impl_->uploadCommandList.Get()};
    impl_->queue->ExecuteCommandLists(1, lists);
    impl_->waitForGpu();
    std::printf("[dbg] readPixels: submitted\n"); std::fflush(stdout);

    const D3D12_RANGE readRange{0, 0};
    void* mapped = nullptr;
    const HRESULT mapHr = stagingBuffer->Map(0, &readRange, &mapped);
    std::printf("[dbg] readPixels: map=0x%08lX\n",(unsigned long)mapHr); std::fflush(stdout);
    if (FAILED(mapHr)) {
        return false;
    }
    const auto* source_bytes = static_cast<const std::uint8_t*>(mapped);
    out.assign(tightPitch * height, 0);
    for (std::uint32_t row = 0; row < height; ++row) {
        std::memcpy(out.data() + static_cast<std::size_t>(row) * tightPitch,
                    source_bytes + static_cast<std::size_t>(row) * rowPitch, tightPitch);
    }
    stagingBuffer->Unmap(0, nullptr);
    impl_->dumpInfoQueue("readPixels");
    return true;
}

}  // namespace Aporia::Backend
