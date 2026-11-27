#pragma once

#include "Aporia/Graphics/Graphics.hpp"

#include <array>

namespace Aporia::graphics_internal {

// Layer-aware UI flattening (the 2D "z-buffer"). It buckets a single UiDrawList
// into one plain UiDrawList per UiLayer, preserving list order inside each layer.
//
// This is the *only* place that knows about layers: RectRenderer, TextRenderer
// and the text pass all keep operating on an ordinary UiDrawList and never see a
// UiLayer. The frame records the buckets low-to-high, so rectangles and text
// interleave by layer instead of always drawing every rect and then every run.
class UiLayerStack {
public:
    void build(const UiDrawList& source);

    [[nodiscard]] const UiDrawList& bucket(UiLayer layer) const noexcept {
        const std::uint32_t index = static_cast<std::uint32_t>(layer);
        return buckets_[index < kUiLayerCount ? index : kUiLayerCount - 1];
    }

    [[nodiscard]] bool empty() const noexcept;

private:
    std::array<UiDrawList, kUiLayerCount> buckets_{};
};

} // namespace Aporia::graphics_internal
