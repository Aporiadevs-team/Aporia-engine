#pragma once

#include "Aporia/Core/Export.hpp"

#include <cstdint>
#include <memory>

namespace Aporia {

enum class KeyCode : std::uint16_t {
    Unknown = 0,
    W,
    A,
    S,
    D,
    Q,
    E,
    R,
    U,
    X,
    M,
    T,
    N,
    P,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Space,
    Shift,
    Control,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Count,
};

enum class MouseButton : std::uint8_t {
    None = 0,
    Left,
    Right,
    Middle,
};

struct MouseState {
    double x = 0.0;
    double y = 0.0;
};

// Polled input facade. States come from a pluggable InputSource so tests and
// non-Windows hosts can inject their own provider.
class APORIA_API Input {
public:
    class Source {
    public:
        virtual ~Source() = default;
        virtual bool keyDown(KeyCode key) = 0;
        virtual bool mouseButtonDown(MouseButton button) = 0;
        virtual MouseState mouse() = 0;
        // Wheel movement accumulated since the last update(); positive = up.
        virtual double scrollDelta() { return 0.0; }
    };

    Input() noexcept;
    ~Input();

    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;
    Input(Input&&) noexcept;
    Input& operator=(Input&&) noexcept;

    // Replaces the state source; passing nullptr installs a dead source.
    void setSource(std::unique_ptr<Source> source) noexcept;
    [[nodiscard]] Source& source() noexcept;

    // Call once per frame before reading edges; updates pressed/released deltas.
    void update() noexcept;

    [[nodiscard]] bool key(KeyCode key) const noexcept;
    [[nodiscard]] bool keyPressed(KeyCode key) const noexcept;
    [[nodiscard]] bool keyReleased(KeyCode key) const noexcept;

    [[nodiscard]] bool mouseButton(MouseButton button) const noexcept;
    [[nodiscard]] bool mouseButtonPressed(MouseButton button) const noexcept;
    [[nodiscard]] bool mouseButtonReleased(MouseButton button) const noexcept;

    [[nodiscard]] MouseState mouse() const noexcept;

    // Wheel delta accumulated since the previous update(); positive = up.
    // Consumed per frame: update() resets the accumulator through the source.
    [[nodiscard]] double scrollDelta() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Win32 polled source bound to a native window (HWND). Returns nullptr off Windows.
APORIA_API std::unique_ptr<Input::Source> makeWin32InputSource(std::uintptr_t nativeWindow);

// SDL3 polled source bound to an SDL_Window*. Only available when the engine is
// built with APORIA_INPUT_SDL3; returns nullptr otherwise or for a null window.
APORIA_API std::unique_ptr<Input::Source> makeSDL3InputSource(void* sdlWindow);

// GLFW polled source bound to a GLFWwindow*. Only available when the engine is
// built with APORIA_INPUT_GLFW and GLFW headers are present.
APORIA_API std::unique_ptr<Input::Source> makeGlfwInputSource(void* glfwWindow);

}
