#include "Aporia/Graphics/Backend.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>

#if defined(APORIA_ENABLE_D3D12)
#include "Device/D3D12/D3D12Backend.hpp"
#endif

namespace Aporia::Backend {

const char* toString(BackendKind kind) {
    switch (kind) {
        case BackendKind::Auto: return "auto";
        case BackendKind::D3D12: return "d3d12";
        case BackendKind::D3D11: return "d3d11";
        case BackendKind::D3D9: return "d3d9";
    }
    return "unknown";
}

BackendKind backendKindFromString(const std::string& text) {
    std::string lowered;
    lowered.reserve(text.size());
    std::transform(text.begin(), text.end(), std::back_inserter(lowered),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (lowered == "auto") return BackendKind::Auto;
    if (lowered == "d3d12" || lowered == "dx12") return BackendKind::D3D12;
    if (lowered == "d3d11" || lowered == "dx11") return BackendKind::D3D11;
    if (lowered == "d3d9" || lowered == "dx9") return BackendKind::D3D9;
    return BackendKind::Auto;
}

std::unique_ptr<GraphicsBackend> createBackend(BackendKind kind) {
    switch (kind) {
#if defined(APORIA_ENABLE_D3D12)
        case BackendKind::D3D12:
            return std::make_unique<D3D12Backend>();
#endif
        default:
            return nullptr;
    }
}

}  // namespace Aporia::Backend
