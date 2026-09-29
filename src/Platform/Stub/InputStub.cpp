#include "Aporia/Input/Input.hpp"

namespace Aporia {

// Non-Windows fallback: input states are only ever provided via setSource.
std::unique_ptr<Input::Source> makeWin32InputSource(std::uintptr_t) {
    return nullptr;
}

}
