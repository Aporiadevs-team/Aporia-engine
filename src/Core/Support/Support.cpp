#include "Support.hpp"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <fstream>

namespace Aporia::graphics_internal {

std::vector<std::uint32_t> readSpirv(const std::string& path, bool& success) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        success = false;
        return {};
    }
    const std::streamoff byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) {
        success = false;
        return {};
    }
    std::vector<std::uint32_t> words(static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(words.data()), byteCount)) {
        success = false;
        return {};
    }
    success = true;
    return words;
}

#if defined(_WIN32)
bool getWindowExtent(HWND window, VkExtent2D& extent) noexcept {
    RECT clientRect{};
    if (window == nullptr || !GetClientRect(window, &clientRect)) {
        return false;
    }
    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;
    if (width <= 0 || height <= 0) {
        return false;
    }
    extent.width = static_cast<std::uint32_t>(width);
    extent.height = static_cast<std::uint32_t>(height);
    return true;
}
#endif

} // namespace Aporia::graphics_internal
