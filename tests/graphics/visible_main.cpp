#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Aporia/Graphics/Graphics.hpp"
#include "Aporia/Graphics/MeshPrimitives.hpp"
#include "Aporia/Math/Matrix4.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr wchar_t windowClassName[] = L"AporiaVisibleRenderTest";
bool running = true;

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
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

Aporia::CameraState makeCamera(double aspect, const Aporia::Vec3& eye, const Aporia::Vec3& target) {
    const Aporia::Mat4 view = Aporia::Mat4::lookAt(eye, target, {0.0, 0.0, 1.0});
    const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(1.0, aspect, 0.1, 200.0);
    Aporia::CameraState camera;
    std::memcpy(camera.viewMatrix, &view, sizeof(view));
    std::memcpy(camera.projectionMatrix, &projection, sizeof(projection));
    camera.position[0] = eye.x;
    camera.position[1] = eye.y;
    camera.position[2] = eye.z;
    return camera;
}

Aporia::Vec3 transformClipPoint(const Aporia::Mat4& matrix, const Aporia::Vec3& point, double& outW) {
    const float x = static_cast<float>(point.x);
    const float y = static_cast<float>(point.y);
    const float z = static_cast<float>(point.z);
    outW = matrix.m[0][3] * x + matrix.m[1][3] * y + matrix.m[2][3] * z + matrix.m[3][3];
    return {
        (matrix.m[0][0] * x + matrix.m[1][0] * y + matrix.m[2][0] * z + matrix.m[3][0]) / outW,
        (matrix.m[0][1] * x + matrix.m[1][1] * y + matrix.m[2][1] * z + matrix.m[3][1]) / outW,
        (matrix.m[0][2] * x + matrix.m[1][2] * y + matrix.m[2][2] * z + matrix.m[3][2]) / outW,
    };
}

}

int main() {
    std::ofstream diagnostics("visible-render-diagnostics.log", std::ios::out | std::ios::trunc);
    if (!diagnostics) {
        MessageBoxW(nullptr, L"Cannot open visible-render-diagnostics.log", L"Render diagnostics", MB_ICONERROR);
        return 2;
    }
    const auto log = [&diagnostics](const std::string& line) {
        diagnostics << line << '\n';
        diagnostics.flush();
    };
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = windowClassName;
    if (RegisterClassExW(&windowClass) == 0) {
        log("RegisterClassExW failed: " + std::to_string(GetLastError()));
        return 1;
    }

    HWND window = CreateWindowExW(0, windowClassName, L"Aporia Vulkan render test",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        log("CreateWindowExW failed: " + std::to_string(GetLastError()));
        UnregisterClassW(windowClassName, instance);
        return 1;
    }

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    Aporia::Graphics graphics;
    Aporia::GraphicsContext context;
    context.nativeWindow = reinterpret_cast<std::uintptr_t>(window);
    Aporia::GraphicsConfig config;
    config.headless = false;
    config.enableValidation = true;
    config.showWindow = true;
    config.quality = Aporia::GraphicsQuality::Low;
    config.sunDirection[0] = 0.0f;
    config.sunDirection[1] = -0.55f;
    config.sunDirection[2] = 0.85f;
    config.shadowExtent = 10.0f;
    if (!graphics.initialize(context, config)) {
        log("Vulkan init failed: " + graphics.lastError());
        DestroyWindow(window);
        UnregisterClassW(windowClassName, instance);
        return 1;
    }

    const auto ground = graphics.createMesh(Aporia::apr::makePlaneMesh(12.0f, 12.0f));
    const auto box = graphics.createMesh(Aporia::apr::makeBoxMesh(0.75f, 0.75f, 0.75f));
    const auto wall = graphics.createMesh(Aporia::apr::makeBoxMesh(0.05f, 5.0f, 3.0f));
    if (!ground.valid() || !box.valid() || !wall.valid()) {
        log("Mesh creation failed: " + graphics.lastError());
        graphics.shutdown();
        DestroyWindow(window);
        UnregisterClassW(windowClassName, instance);
        return 1;
    }

    Aporia::EnvironmentState environment;
    environment.sunDirection[0] = config.sunDirection[0];
    environment.sunDirection[1] = config.sunDirection[1];
    environment.sunDirection[2] = config.sunDirection[2];
    environment.sunIntensity = 1.0f;
    environment.mirrorZ = 0.0f;

    const Aporia::MeshData diagnosticBox = Aporia::apr::makeBoxMesh(0.75f, 0.75f, 0.75f);
    double sourceMinimum[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    double sourceMaximum[3] = {-sourceMinimum[0], -sourceMinimum[1], -sourceMinimum[2]};
    bool indicesValid = diagnosticBox.indices.size() == 36;
    for (const auto index : diagnosticBox.indices) {
        indicesValid = indicesValid && index < diagnosticBox.vertices.size();
    }
    for (const auto& vertex : diagnosticBox.vertices) {
        for (int axis = 0; axis < 3; ++axis) {
            sourceMinimum[axis] = std::min(sourceMinimum[axis], static_cast<double>(vertex.position[axis]));
            sourceMaximum[axis] = std::max(sourceMaximum[axis], static_cast<double>(vertex.position[axis]));
        }
    }
    bool windingOutward = indicesValid;
    bool facesPlanar = indicesValid;
    double minTriangleNormalDot = std::numeric_limits<double>::infinity();
    for (std::size_t triangle = 0; triangle + 2 < diagnosticBox.indices.size(); triangle += 3) {
        const auto& a = diagnosticBox.vertices[diagnosticBox.indices[triangle]];
        const auto& b = diagnosticBox.vertices[diagnosticBox.indices[triangle + 1]];
        const auto& c = diagnosticBox.vertices[diagnosticBox.indices[triangle + 2]];
        const Aporia::Vec3 ab{b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]};
        const Aporia::Vec3 ac{c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]};
        const Aporia::Vec3 normal{a.normal[0], a.normal[1], a.normal[2]};
        const double normalDot = dot(cross(ab, ac), normal);
        minTriangleNormalDot = std::min(minTriangleNormalDot, normalDot);
        windingOutward = windingOutward && normalDot > 0.0;
        const double expectedOffset = 0.75 * (std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z));
        const auto onDeclaredPlane = [&](const Aporia::MeshVertex& vertex) {
            return std::abs(dot(Aporia::Vec3{vertex.position[0], vertex.position[1], vertex.position[2]}, normal) - expectedOffset) < 1.0e-6;
        };
        facesPlanar = facesPlanar && onDeclaredPlane(a) && onDeclaredPlane(b) && onDeclaredPlane(c);
    }
    {
        std::ostringstream line;
        line << std::setprecision(7)
              << "[mesh] stride=" << sizeof(Aporia::MeshVertex)
              << " offsets=" << offsetof(Aporia::MeshVertex, position) << ','
              << offsetof(Aporia::MeshVertex, normal) << ',' << offsetof(Aporia::MeshVertex, uv)
               << " vertices=" << diagnosticBox.vertices.size() << " indices=" << diagnosticBox.indices.size()
              << " indexBoundsValid=" << indicesValid << " windingOutward=" << windingOutward
              << " facesPlanar=" << facesPlanar << " minTriangleNormalDot=" << minTriangleNormalDot
              << " positionBounds=[" << sourceMinimum[0] << ',' << sourceMinimum[1] << ',' << sourceMinimum[2]
              << "]..[" << sourceMaximum[0] << ',' << sourceMaximum[1] << ',' << sourceMaximum[2] << ']';
        log(line.str());
    }

    log("Visible Vulkan render test running. Close the window to exit.");
    MSG message{};
    double phase = 0.0;
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

        RECT client{};
        GetClientRect(window, &client);
        const double width = std::max<LONG>(1, client.right - client.left);
        const double height = std::max<LONG>(1, client.bottom - client.top);
        const auto camera = makeCamera(width / height, {7.5, -10.0, 7.0}, {0.0, 1.0, 0.8});

        Aporia::RenderItem groundItem;
        groundItem.meshId = ground.value();
        const Aporia::Mat4 groundModel = Aporia::Mat4::identity();
        std::memcpy(groundItem.modelMatrix, &groundModel, sizeof(groundModel));

        Aporia::RenderItem boxItem;
        boxItem.meshId = box.value();
        Aporia::Mat4 boxModel = Aporia::Mat4::translation({0.0, 1.0, 0.78});
        if (GetCommandLineW() != nullptr && wcsstr(GetCommandLineW(), L"dynamic") != nullptr) {
            phase += 0.02;
            boxModel = Aporia::Mat4::translation({1.5 * std::cos(phase), 1.5 * std::sin(phase),
                                                  0.9 + 0.4 * std::sin(phase * 1.7)});
        }
        std::memcpy(boxItem.modelMatrix, &boxModel, sizeof(boxModel));

        std::vector<Aporia::RenderItem> items{groundItem, boxItem};
        if (GetCommandLineW() != nullptr && wcsstr(GetCommandLineW(), L"reflection") != nullptr) {
            Aporia::RenderItem wallItem;
            wallItem.meshId = wall.value();
            const Aporia::Mat4 wallModel = Aporia::Mat4::translation({0.0, 5.0, 3.0});
            std::memcpy(wallItem.modelMatrix, &wallModel, sizeof(wallModel));
            items.push_back(wallItem);
        }

        if (phase == 0.0 || (static_cast<int>(phase * 50.0) % 120) == 0) {
            const Aporia::Mat4 view = Aporia::Mat4::lookAt({7.5, -10.0, 7.0}, {0.0, 1.0, 0.8}, {0.0, 0.0, 1.0});
            const Aporia::Mat4 projection = Aporia::Mat4::perspectiveVulkan(1.0, width / height, 0.1, 200.0);
            const Aporia::Mat4 modelViewProjection = projection * view * boxModel;
            double minNdc[3] = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
            double maxNdc[3] = {-minNdc[0], -minNdc[1], -minNdc[2]};
            for (int corner = 0; corner < 8; ++corner) {
                const Aporia::Vec3 local{
                    (corner & 1) ? 0.75 : -0.75,
                    (corner & 2) ? 0.75 : -0.75,
                    (corner & 4) ? 0.75 : -0.75};
                const Aporia::Vec3 world = boxModel.transformPoint(local);
                double clipW = 0.0;
                const Aporia::Vec3 ndc = transformClipPoint(modelViewProjection, local, clipW);
                for (int axis = 0; axis < 3; ++axis) {
                    minNdc[axis] = std::min(minNdc[axis], axis == 0 ? ndc.x : axis == 1 ? ndc.y : ndc.z);
                    maxNdc[axis] = std::max(maxNdc[axis], axis == 0 ? ndc.x : axis == 1 ? ndc.y : ndc.z);
                }
                if (corner == 0) {
                    std::ostringstream line;
                    line << std::setprecision(7) << "[corner] local=(-.75,-.75,-.75) world=(" << world.x << ',' << world.y << ',' << world.z
                         << ") clipW=" << clipW << " ndc=(" << ndc.x << ',' << ndc.y << ',' << ndc.z << ')';
                    log(line.str());
                }
            }
            std::ostringstream line;
            line << std::setprecision(7) << "[frame] modelTranslation=(" << boxModel.m[3][0] << ',' << boxModel.m[3][1] << ',' << boxModel.m[3][2]
                 << ") ndcBounds=[" << minNdc[0] << ',' << minNdc[1] << ',' << minNdc[2]
                 << "]..[" << maxNdc[0] << ',' << maxNdc[1] << ',' << maxNdc[2] << ']';
            log(line.str());
        }

        if (!graphics.render(camera, environment, items)) {
            log("Vulkan render failed: " + graphics.lastError());
            break;
        }
        Sleep(16);
    }

    graphics.shutdown();
    DestroyWindow(window);
    UnregisterClassW(windowClassName, instance);
    return 0;
}
