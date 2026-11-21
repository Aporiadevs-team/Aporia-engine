#include "Aporia/Input/Input.hpp"

#if defined(__has_include)
#  if __has_include(<GLFW/glfw3.h>)
#    define APORIA_HAS_GLFW_HEADERS 1
#    include <GLFW/glfw3.h>
#  endif
#endif

namespace Aporia {
namespace {

#ifdef APORIA_HAS_GLFW_HEADERS
int glfwKeyFor(KeyCode key) noexcept {
    switch (key) {
    case KeyCode::W: return GLFW_KEY_W;
    case KeyCode::A: return GLFW_KEY_A;
    case KeyCode::S: return GLFW_KEY_S;
    case KeyCode::D: return GLFW_KEY_D;
    case KeyCode::Q: return GLFW_KEY_Q;
    case KeyCode::E: return GLFW_KEY_E;
    case KeyCode::R: return GLFW_KEY_R;
    case KeyCode::U: return GLFW_KEY_U;
    case KeyCode::X: return GLFW_KEY_X;
    case KeyCode::M: return GLFW_KEY_M;
    case KeyCode::T: return GLFW_KEY_T;
    case KeyCode::N: return GLFW_KEY_N;
    case KeyCode::P: return GLFW_KEY_P;
    case KeyCode::Digit1: return GLFW_KEY_1;
    case KeyCode::Digit2: return GLFW_KEY_2;
    case KeyCode::Digit3: return GLFW_KEY_3;
    case KeyCode::Digit4: return GLFW_KEY_4;
    case KeyCode::Digit5: return GLFW_KEY_5;
    case KeyCode::Space: return GLFW_KEY_SPACE;
    case KeyCode::Shift: return GLFW_KEY_LEFT_SHIFT;
    case KeyCode::Control: return GLFW_KEY_LEFT_CONTROL;
    case KeyCode::Escape: return GLFW_KEY_ESCAPE;
    case KeyCode::Up: return GLFW_KEY_UP;
    case KeyCode::Down: return GLFW_KEY_DOWN;
    case KeyCode::Left: return GLFW_KEY_LEFT;
    case KeyCode::Right: return GLFW_KEY_RIGHT;
    default: return GLFW_KEY_UNKNOWN;
    }
}

class GlfwSource final : public Input::Source {
public:
    explicit GlfwSource(GLFWwindow* window) : window_(window) {}

    bool keyDown(KeyCode key) override {
        const int glfwKey = glfwKeyFor(key);
        return glfwKey != GLFW_KEY_UNKNOWN && glfwGetKey(window_, glfwKey) == GLFW_PRESS;
    }

    bool mouseButtonDown(MouseButton button) override {
        int glfwButton = GLFW_KEY_UNKNOWN;
        switch (button) {
        case MouseButton::Left: glfwButton = GLFW_MOUSE_BUTTON_LEFT; break;
        case MouseButton::Right: glfwButton = GLFW_MOUSE_BUTTON_RIGHT; break;
        case MouseButton::Middle: glfwButton = GLFW_MOUSE_BUTTON_MIDDLE; break;
        default: return false;
        }
        return glfwGetMouseButton(window_, glfwButton) == GLFW_PRESS;
    }

    MouseState mouse() override {
        double x = 0.0;
        double y = 0.0;
        glfwGetCursorPos(window_, &x, &y);
        return {x, y};
    }

private:
    GLFWwindow* window_ = nullptr;
};
#endif

} // namespace

std::unique_ptr<Input::Source> makeGlfwInputSource(void* glfwWindow) {
#ifdef APORIA_HAS_GLFW_HEADERS
    if (glfwWindow == nullptr) {
        return nullptr;
    }
    return std::make_unique<GlfwSource>(static_cast<GLFWwindow*>(glfwWindow));
#else
    (void)glfwWindow;
    return nullptr;
#endif
}

} // namespace Aporia
