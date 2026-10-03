#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Aporia/Input/Input.hpp"

#include <array>
#include <atomic>
#include <utility>

namespace Aporia {
namespace {

using KeyStates = std::array<bool, static_cast<std::size_t>(KeyCode::Count)>;
using ButtonStates = std::array<bool, 4>;

class PollSource : public Input::Source {
public:
    explicit PollSource(HWND window)
        : window_(window) {
        installHook();
    }

    ~PollSource() override {
        uninstallHook();
    }

    bool keyDown(KeyCode key) override {
        const int virtualKey = virtualKeyFor(key);
        if (virtualKey == 0) {
            return false;
        }
        return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    }

    bool mouseButtonDown(MouseButton button) override {
        const int virtualKey = mouseButtonKey(button);
        if (virtualKey == 0) {
            return false;
        }
        return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    }

    MouseState mouse() override {
        RECT client{};
        POINT cursor{};
        GetCursorPos(&cursor);
        if (window_ != nullptr && ScreenToClient(window_, &cursor)) {
            return {static_cast<double>(cursor.x), static_cast<double>(cursor.y)};
        }
        return {static_cast<double>(cursor.x), static_cast<double>(cursor.y)};
    }

    double scrollDelta() override {
        // Take everything the hook accumulated since the last frame.
        const double delta = scrollAccumulator_.exchange(0.0);
        return delta / static_cast<double>(WHEEL_DELTA);
    }

private:
    static LRESULT CALLBACK lowLevelMouseProcedure(
        int code, WPARAM wParam, LPARAM lParam) {
        if (code == HC_ACTION && wParam == WM_MOUSEWHEEL) {
            const MSLLHOOKSTRUCT* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            const double delta = static_cast<short>(HIWORD(info->mouseData));
            if (PollSource* source = activeSource.load(std::memory_order_acquire)) {
                // Accumulate raw wheel units; the consumer divides by WHEEL_DELTA.
                double current = source->scrollAccumulator_.load(std::memory_order_relaxed);
                while (!source->scrollAccumulator_.compare_exchange_weak(
                        current, current + delta, std::memory_order_relaxed)) {
                }
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void installHook() {
        // The low-level hook receives wheel events even when the window is
        // unfocused; one instance at a time keeps the global pointer simple.
        if (activeSource.load(std::memory_order_acquire) != nullptr) {
            return;
        }
        hook_ = SetWindowsHookExW(
            WH_MOUSE_LL, lowLevelMouseProcedure, GetModuleHandleW(nullptr), 0);
        if (hook_ != nullptr) {
            activeSource.store(this, std::memory_order_release);
        }
    }

    void uninstallHook() noexcept {
        if (activeSource.load(std::memory_order_acquire) == this) {
            activeSource.store(nullptr, std::memory_order_release);
            if (hook_ != nullptr) {
                UnhookWindowsHookEx(hook_);
                hook_ = nullptr;
            }
        }
    }

    HWND window_;
    HHOOK hook_ = nullptr;
    std::atomic<double> scrollAccumulator_{0.0};
    static inline std::atomic<PollSource*> activeSource{nullptr};

    static int virtualKeyFor(KeyCode key) noexcept {
        switch (key) {
        case KeyCode::W: return 'W';
        case KeyCode::A: return 'A';
        case KeyCode::S: return 'S';
        case KeyCode::D: return 'D';
        case KeyCode::Q: return 'Q';
        case KeyCode::E: return 'E';
        case KeyCode::R: return 'R';
        case KeyCode::U: return 'U';
        case KeyCode::X: return 'X';
        case KeyCode::M: return 'M';
        case KeyCode::Digit1: return '1';
        case KeyCode::Digit2: return '2';
        case KeyCode::Digit3: return '3';
        case KeyCode::Space: return VK_SPACE;
        case KeyCode::Shift: return VK_SHIFT;
        case KeyCode::Control: return VK_CONTROL;
        case KeyCode::Escape: return VK_ESCAPE;
        case KeyCode::Up: return VK_UP;
        case KeyCode::Down: return VK_DOWN;
        case KeyCode::Left: return VK_LEFT;
        case KeyCode::Right: return VK_RIGHT;
        default: return 0;
        }
    }

    static int mouseButtonKey(MouseButton button) noexcept {
        switch (button) {
        case MouseButton::Left: return VK_LBUTTON;
        case MouseButton::Right: return VK_RBUTTON;
        case MouseButton::Middle: return VK_MBUTTON;
        default: return 0;
        }
    }
};

class DeadSource final : public Input::Source {
public:
    bool keyDown(KeyCode) override { return false; }
    bool mouseButtonDown(MouseButton) override { return false; }
    MouseState mouse() override { return {}; }
};

}

struct Input::Impl {
    KeyStates current{};
    KeyStates previous{};
    ButtonStates currentButtons{};
    ButtonStates previousButtons{};
    MouseState mouseState{};
    double scrollDelta = 0.0;
    std::unique_ptr<Source> source = std::make_unique<DeadSource>();

    void poll() {
        for (std::size_t index = 0; index < current.size(); ++index) {
            previous[index] = current[index];
            current[index] = source->keyDown(static_cast<KeyCode>(index));
        }
        for (std::size_t index = 0; index < currentButtons.size(); ++index) {
            previousButtons[index] = currentButtons[index];
            currentButtons[index] = source->mouseButtonDown(static_cast<MouseButton>(index));
        }
        mouseState = source->mouse();
        scrollDelta = source->scrollDelta();
    }
};

Input::Input() noexcept
    : impl_(std::make_unique<Impl>()) {
}

Input::~Input() = default;
Input::Input(Input&&) noexcept = default;
Input& Input::operator=(Input&&) noexcept = default;

void Input::setSource(std::unique_ptr<Source> source) noexcept {
    impl_->source = source != nullptr ? std::move(source) : std::make_unique<DeadSource>();
}

Input::Source& Input::source() noexcept {
    return *impl_->source;
}

void Input::update() noexcept {
    impl_->poll();
}

bool Input::key(KeyCode key) const noexcept {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < impl_->current.size() && impl_->current[index];
}

bool Input::keyPressed(KeyCode key) const noexcept {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < impl_->current.size() && impl_->current[index] && !impl_->previous[index];
}

bool Input::keyReleased(KeyCode key) const noexcept {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < impl_->current.size() && !impl_->current[index] && impl_->previous[index];
}

bool Input::mouseButton(MouseButton button) const noexcept {
    const std::size_t index = static_cast<std::size_t>(button);
    return index < impl_->currentButtons.size() && impl_->currentButtons[index];
}

bool Input::mouseButtonPressed(MouseButton button) const noexcept {
    const std::size_t index = static_cast<std::size_t>(button);
    return index < impl_->currentButtons.size() && impl_->currentButtons[index] && !impl_->previousButtons[index];
}

bool Input::mouseButtonReleased(MouseButton button) const noexcept {
    const std::size_t index = static_cast<std::size_t>(button);
    return index < impl_->currentButtons.size() && !impl_->currentButtons[index] && impl_->previousButtons[index];
}

MouseState Input::mouse() const noexcept {
    return impl_->mouseState;
}

double Input::scrollDelta() const noexcept {
    return impl_->scrollDelta;
}

std::unique_ptr<Input::Source> makeWin32InputSource(std::uintptr_t nativeWindow) {
    return std::make_unique<PollSource>(reinterpret_cast<HWND>(nativeWindow));
}

}
