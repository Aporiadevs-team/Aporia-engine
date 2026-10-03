#pragma once

#include "Aporia/Graphics/Graphics.hpp"

namespace Aporia::graphics_internal {

class TextRenderer {
public:
    void setAtlas(const UiTextAtlasDesc& atlas) { atlas_ = atlas; }
    [[nodiscard]] const UiTextAtlasDesc& atlas() const noexcept { return atlas_; }

private:
    UiTextAtlasDesc atlas_{};
};

}
