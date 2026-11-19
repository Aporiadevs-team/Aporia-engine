#include "UiLayerStack.hpp"

namespace Aporia::graphics_internal {

void UiLayerStack::build(const UiDrawList& source) {
    // Global config (font, atlas, corner radius, blur) is shared by every layer;
    // only the rect/text lists are split. Clearing the vectors keeps the buckets'
    // capacity across frames.
    for (UiDrawList& bucket : buckets_) {
        bucket.roundedRects.clear();
        bucket.textRuns.clear();
        bucket.textAtlas = source.textAtlas;
        bucket.font = source.font;
        bucket.cornerRadius = source.cornerRadius;
        bucket.blur = source.blur;
    }

    for (const auto& rect : source.roundedRects) {
        const std::uint32_t index = static_cast<std::uint32_t>(rect.first.layer);
        buckets_[index < kUiLayerCount ? index : kUiLayerCount - 1].roundedRects.push_back(rect);
    }
    for (const UiTextRun& run : source.textRuns) {
        const std::uint32_t index = static_cast<std::uint32_t>(run.layer);
        buckets_[index < kUiLayerCount ? index : kUiLayerCount - 1].textRuns.push_back(run);
    }
}

bool UiLayerStack::empty() const noexcept {
    for (const UiDrawList& bucket : buckets_) {
        if (!bucket.roundedRects.empty() || !bucket.textRuns.empty()) {
            return false;
        }
    }
    return true;
}

} // namespace Aporia::graphics_internal
