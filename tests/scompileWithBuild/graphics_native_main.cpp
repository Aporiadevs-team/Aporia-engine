#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Aporia/Graphics/Graphics.hpp"

#include <cstdint>
#include <iostream>

namespace {

constexpr wchar_t windowClassName[] = L"AporiaVulkanNativeSmoke";

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

int main() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = windowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        std::cerr << "RegisterClassExW failed: " << GetLastError() << '\n';
        return 1;
    }

    HWND window = CreateWindowExW(
        0,
        windowClassName,
        L"Aporia Vulkan Native Smoke",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        480,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (window == nullptr) {
        std::cerr << "CreateWindowExW failed: " << GetLastError() << '\n';
        UnregisterClassW(windowClassName, instance);
        return 1;
    }

    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);

    Aporia::Graphics graphics;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(window);
    Aporia::GraphicsConfig config;
    config.headless = false;
    config.enableValidation = true;

    int result = 0;
    if (!graphics.initialize(context, config)) {
        std::cerr << "native Vulkan initialization failed: " << graphics.lastError() << '\n';
        result = 1;
    } else {
        constexpr std::uint8_t texturePixels[] = {
            0, 255, 255, 255,
            255, 0, 255, 255,
            255, 255, 0, 255,
            255, 255, 255, 255,
        };
        Aporia::TextureDesc textureDescription;
        textureDescription.width = 2;
        textureDescription.height = 2;
        textureDescription.format = Aporia::TextureFormat::Rgba8Srgb;
        textureDescription.pixels = texturePixels;
        textureDescription.dataSize = sizeof(texturePixels);
        const Aporia::TextureHandle texture = graphics.createTexture2D(textureDescription);
        if (!texture.valid() || !graphics.setTexture2D(texture) ||
            !graphics.updateTexture2D(texture, texturePixels, sizeof(texturePixels))) {
            std::cerr << "native Vulkan texture setup failed: " << graphics.lastError() << '\n';
            result = 1;
        }

        for (int frame = 0; frame < 4; ++frame) {
            if (!graphics.renderFrame()) {
                std::cerr << "native Vulkan frame failed: " << graphics.lastError() << '\n';
                result = 1;
                break;
            }
        }

        if (result == 0) {
            SetWindowPos(window, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            UpdateWindow(window);
            bool resizedFrame = false;
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (graphics.renderFrame()) {
                    resizedFrame = true;
                    break;
                }
                Sleep(10);
            }
            if (!resizedFrame) {
                std::cerr << "native Vulkan resize frame failed: " << graphics.lastError() << '\n';
                result = 1;
            }
        }

        if (result == 0) {
            graphics.destroyTexture2D(texture);
            if (graphics.stats().textureCount != 1 || !graphics.renderFrame()) {
                std::cerr << "native Vulkan default texture frame failed: " << graphics.lastError() << '\n';
                result = 1;
            }
        }

        const Aporia::GraphicsStats stats = graphics.stats();
        if (stats.nativeWindow == false || stats.headless == true ||
            stats.surfaceWidth < 700 || stats.surfaceHeight < 500 ||
            stats.presentCount < 5 || stats.frameCount < 5) {
            std::cerr << "native Vulkan stats are invalid: " << stats.surfaceWidth << "x"
                      << stats.surfaceHeight << " frames=" << stats.frameCount
                      << " presents=" << stats.presentCount << '\n';
            result = 1;
        }
        graphics.shutdown();
    }

    DestroyWindow(window);
    UnregisterClassW(windowClassName, instance);
    if (result == 0) {
        std::cout << "Aporia native graphics smoke test passed\n";
    }
    return result;
}
