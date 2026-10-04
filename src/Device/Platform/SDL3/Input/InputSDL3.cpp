#include "Aporia/Input/Input.hpp"

#include <SDL3/SDL.h>

namespace Aporia {
namespace {

SDL_Scancode scancodeFor(KeyCode key) noexcept {
    switch (key) {
    case KeyCode::W: return SDL_SCANCODE_W;
    case KeyCode::A: return SDL_SCANCODE_A;
    case KeyCode::S: return SDL_SCANCODE_S;
    case KeyCode::D: return SDL_SCANCODE_D;
    case KeyCode::Q: return SDL_SCANCODE_Q;
    case KeyCode::E: return SDL_SCANCODE_E;
    case KeyCode::R: return SDL_SCANCODE_R;
    case KeyCode::U: return SDL_SCANCODE_U;
    case KeyCode::X: return SDL_SCANCODE_X;
    case KeyCode::M: return SDL_SCANCODE_M;
    case KeyCode::T: return SDL_SCANCODE_T;
    case KeyCode::N: return SDL_SCANCODE_N;
    case KeyCode::P: return SDL_SCANCODE_P;
    case KeyCode::Digit1: return SDL_SCANCODE_1;
    case KeyCode::Digit2: return SDL_SCANCODE_2;
    case KeyCode::Digit3: return SDL_SCANCODE_3;
    case KeyCode::Digit4: return SDL_SCANCODE_4;
    case KeyCode::Digit5: return SDL_SCANCODE_5;
    case KeyCode::Space: return SDL_SCANCODE_SPACE;
    case KeyCode::Shift: return SDL_SCANCODE_LSHIFT;
    case KeyCode::Control: return SDL_SCANCODE_LCTRL;
    case KeyCode::Escape: return SDL_SCANCODE_ESCAPE;
    case KeyCode::Up: return SDL_SCANCODE_UP;
    case KeyCode::Down: return SDL_SCANCODE_DOWN;
    case KeyCode::Left: return SDL_SCANCODE_LEFT;
    case KeyCode::Right: return SDL_SCANCODE_RIGHT;
    default: return SDL_SCANCODE_UNKNOWN;
    }
}

class SDL3Source final : public Input::Source {
public:
    bool keyDown(KeyCode key) override {
        const bool* state = SDL_GetKeyboardState(nullptr);
        if (state == nullptr) {
            return false;
        }
        const SDL_Scancode scancode = scancodeFor(key);
        return scancode != SDL_SCANCODE_UNKNOWN && state[scancode];
    }

    bool mouseButtonDown(MouseButton button) override {
        const SDL_MouseButtonFlags flags = SDL_GetMouseState(nullptr, nullptr);
        switch (button) {
        case MouseButton::Left: return (flags & SDL_BUTTON_LMASK) != 0;
        case MouseButton::Right: return (flags & SDL_BUTTON_RMASK) != 0;
        case MouseButton::Middle: return (flags & SDL_BUTTON_MMASK) != 0;
        default: return false;
        }
    }

    MouseState mouse() override {
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState(&x, &y);
        return {static_cast<double>(x), static_cast<double>(y)};
    }
};

} // namespace

std::unique_ptr<Input::Source> makeSDL3InputSource(void* sdlWindow) {
    if (sdlWindow == nullptr) {
        return nullptr;
    }
    return std::make_unique<SDL3Source>();
}

} // namespace Aporia
