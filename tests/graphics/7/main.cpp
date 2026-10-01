#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kClassName[] = L"AporiaRenderTest7Window";
bool running = true;
bool settingsOpen = false;
Aporia::Graphics* graphics = nullptr;
Aporia::GraphicsQuality quality = Aporia::GraphicsQuality::High;
bool playerUltra = false;
SHORT previousEscape = 0;
SHORT previousKeys[4] = {};

const wchar_t* qualityName(Aporia::GraphicsQuality value) {
    switch (value) {
    case Aporia::GraphicsQuality::Low: return L"Low";
    case Aporia::GraphicsQuality::Medium: return L"Medium";
    case Aporia::GraphicsQuality::High: return L"High";
    }
    return L"Unknown";
}

void applyGraphicsSettings() {
    if (graphics == nullptr) {
        return;
    }
    graphics->setQuality(quality);
    graphics->enablePlayerUltraShadows(playerUltra);
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        // Esc is handled by the polling edge below. Do not let the default
        // window procedure turn it into a close action.
        return 0;
    }
    if (message == WM_CLOSE) {
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

Aporia::CameraState cameraFor(double aspect, double time) {
    const Aporia::Vec3 eye{8.0 * std::cos(time * 0.18), -10.0 * std::sin(time * 0.18), 6.5};
    const Aporia::Vec3 target{0.0, 0.0, 1.0};
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(1.0, aspect, 0.1, 200.0);
    Aporia::CameraState result;
    std::memcpy(result.viewMatrix, &view, sizeof(view));
    std::memcpy(result.projectionMatrix, &projection, sizeof(projection));
    result.position[0] = eye.x;
    result.position[1] = eye.y;
    result.position[2] = eye.z;
    return result;
}

void addItem(std::vector<Aporia::RenderItem>& items, Aporia::MeshHandle mesh,
             const Aporia::Mat4& model, const float color[4], bool player = false) {
    Aporia::RenderItem item;
    item.meshId = mesh.value();
    std::memcpy(item.modelMatrix, &model, sizeof(model));
    std::memcpy(item.color, color, sizeof(item.color));
    item.playerShadowCaster = player;
    items.push_back(item);
}

void pollSettingsKeys(HWND window) {
    const SHORT escape = GetAsyncKeyState(VK_ESCAPE);
    if ((escape & 0x8000) != 0 && (previousEscape & 0x8000) == 0) {
        settingsOpen = !settingsOpen;
        InvalidateRect(window, nullptr, FALSE);
    }
    previousEscape = escape;
    const int keys[] = {'1', '2', '3', 'U'};
    for (int index = 0; index < 4; ++index) {
        const SHORT state = GetAsyncKeyState(keys[index]);
        if (settingsOpen && (state & 0x8000) != 0 && (previousKeys[index] & 0x8000) == 0) {
            if (index < 3) {
                quality = index == 0 ? Aporia::GraphicsQuality::Low :
                          index == 1 ? Aporia::GraphicsQuality::Medium : Aporia::GraphicsQuality::High;
            } else {
                playerUltra = !playerUltra;
            }
            applyGraphicsSettings();
            InvalidateRect(window, nullptr, FALSE);
        }
        previousKeys[index] = state;
    }
}

}

int main() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = kClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        return 1;
    }

    HWND window = CreateWindowExW(0, kClassName, L"Aporia Render Test 7 - Graphics Lab",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        UnregisterClassW(kClassName, instance);
        return 1;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    Aporia::Graphics localGraphics;
    graphics = &localGraphics;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(window);
    Aporia::GraphicsConfig config;
    config.headless = false;
    config.enableValidation = true;
    config.showWindow = true;
    config.quality = quality;
    config.sunDirection[0] = 0.2f;
    config.sunDirection[1] = -0.7f;
    config.sunDirection[2] = 0.75f;
    config.shadowExtent = 24.0f;
    if (!localGraphics.initialize(context, config)) {
        std::cerr << "Graphics initialization failed: " << localGraphics.lastError() << '\n';
        DestroyWindow(window);
        UnregisterClassW(kClassName, instance);
        return 1;
    }

    const Aporia::MeshHandle ground = localGraphics.createMesh(Aporia::apr::makePlaneMesh(28.0f, 28.0f));
    const Aporia::MeshHandle cube = localGraphics.createMesh(Aporia::apr::makeBoxMesh(0.7f, 0.7f, 0.7f));
    const Aporia::MeshHandle pillar = localGraphics.createMesh(Aporia::apr::makeBoxMesh(0.45f, 0.45f, 2.0f));
    const Aporia::MeshHandle sphere = localGraphics.createMesh(Aporia::apr::makeSphereMesh(0.65f, 24, 16));
    const float white[4] = {0.85f, 0.85f, 0.85f, 1.0f};
    const float playerColor[4] = {0.2f, 0.65f, 1.0f, 1.0f};
    const float red[4] = {1.0f, 0.18f, 0.12f, 1.0f};
    const float gold[4] = {1.0f, 0.65f, 0.12f, 1.0f};

    MSG message{};
    double time = 0.0;
    LARGE_INTEGER frequency{}, previous{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&previous);
    std::cout << "Render Test 7 running. ESC settings, 1/2/3 quality, U player ultra shadows.\n";

    while (running) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) {
            break;
        }
        pollSettingsKeys(window);

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        time += static_cast<double>(now.QuadPart - previous.QuadPart) / static_cast<double>(frequency.QuadPart);
        previous = now;

        RECT client{};
        GetClientRect(window, &client);
        const double width = std::max<LONG>(1, client.right - client.left);
        const double height = std::max<LONG>(1, client.bottom - client.top);
        const Aporia::CameraState camera = cameraFor(width / height, time);
        std::vector<Aporia::RenderItem> items;
        addItem(items, ground, Aporia::Mat4::identity(), white);

        const Aporia::Mat4 playerModel = Aporia::Mat4::translation({
            2.8 * std::cos(time * 0.8),
            2.8 * std::sin(time * 0.8),
            0.8 + 0.5 * std::sin(time * 1.7)});
        addItem(items, cube, playerModel, playerColor, true);

        for (int index = 0; index < 6; ++index) {
            const double angle = static_cast<double>(index) * 1.0471975512 + time * 0.22;
            const double radius = 4.5;
            const Aporia::Mat4 model = Aporia::Mat4::translation({
                radius * std::cos(angle), radius * std::sin(angle), 2.0});
            addItem(items, pillar, model, index % 2 == 0 ? red : gold);
        }

        const Aporia::Mat4 sphereModel = Aporia::Mat4::translation({
            -2.0 * std::cos(time), 1.5 * std::sin(time * 0.7), 1.0 + 0.8 * std::abs(std::sin(time))});
        addItem(items, sphere, sphereModel, gold);

        Aporia::UiDrawList ui;
        if (settingsOpen) {
            ui.cornerRadius = 12.0f;
            ui.blur.mode = Aporia::BlurMode::Kawase;
            ui.blur.radius = 5.0f;
            ui.blur.intensity = 0.9f;
            ui.roundedRects.push_back({{24.0f, 24.0f, 370.0f, 185.0f}, {0.04f, 0.06f, 0.10f, 0.94f}});
            ui.textRuns.push_back({"GRAPHICS SETTINGS", 48.0f, 48.0f, 24.0f, {0.95f, 0.97f, 1.0f, 1.0f}});
            ui.textRuns.push_back({"1 Low    2 Medium    3 High", 48.0f, 88.0f, 16.0f, {0.85f, 0.88f, 0.94f, 1.0f}});
            ui.textRuns.push_back({std::string("U Player ultra shadows: ") + (playerUltra ? "ON" : "OFF"), 48.0f, 116.0f, 16.0f, {0.85f, 0.88f, 0.94f, 1.0f}});
            ui.textRuns.push_back({std::string("Current preset: ") + (quality == Aporia::GraphicsQuality::Low ? "Low" : quality == Aporia::GraphicsQuality::Medium ? "Medium" : "High"), 48.0f, 144.0f, 16.0f, {0.85f, 0.88f, 0.94f, 1.0f}});
            ui.textRuns.push_back({"ESC close settings", 48.0f, 174.0f, 14.0f, {0.65f, 0.70f, 0.78f, 1.0f}});
        }
        localGraphics.setUiDrawList(std::move(ui));

        if (!localGraphics.render(camera, Aporia::EnvironmentState{}, items)) {
            std::cerr << "Render failed: " << localGraphics.lastError() << '\n';
            break;
        }
        Sleep(8);
    }

    localGraphics.shutdown();
    graphics = nullptr;
    DestroyWindow(window);
    UnregisterClassW(kClassName, instance);
    return 0;
}
