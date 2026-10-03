#include "FontAtlasBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string_view>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Aporia::graphics_internal {

namespace {

// --- Minimal JSON parser (sufficient for msdf-atlas-gen output) -------------

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string text;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    [[nodiscard]] const JsonValue* find(std::string_view key) const {
        for (const auto& [name, value] : object) {
            if (name == key) {
                return &value;
            }
        }
        return nullptr;
    }
    [[nodiscard]] double numberOr(double fallback) const {
        return type == Type::Number ? number : fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    JsonValue parse() {
        skipWhitespace();
        return parseValue();
    }

private:
    std::string_view input_;
    std::size_t pos_ = 0;

    void skipWhitespace() {
        while (pos_ < input_.size()) {
            const char c = input_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    char peek() const { return pos_ < input_.size() ? input_[pos_] : '\0'; }

    JsonValue parseValue() {
        skipWhitespace();
        const char c = peek();
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseStringValue();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n') { pos_ += 4; return {}; }
        return parseNumber();
    }

    JsonValue parseObject() {
        JsonValue value;
        value.type = JsonValue::Type::Object;
        ++pos_; // {
        skipWhitespace();
        if (peek() == '}') { ++pos_; return value; }
        while (pos_ < input_.size()) {
            skipWhitespace();
            if (peek() != '"') break;
            const std::string key = parseRawString();
            skipWhitespace();
            if (peek() == ':') ++pos_;
            value.object.emplace_back(key, parseValue());
            skipWhitespace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            break;
        }
        return value;
    }

    JsonValue parseArray() {
        JsonValue value;
        value.type = JsonValue::Type::Array;
        ++pos_; // [
        skipWhitespace();
        if (peek() == ']') { ++pos_; return value; }
        while (pos_ < input_.size()) {
            value.array.push_back(parseValue());
            skipWhitespace();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            break;
        }
        return value;
    }

    JsonValue parseStringValue() {
        JsonValue value;
        value.type = JsonValue::Type::String;
        value.text = parseRawString();
        return value;
    }

    std::string parseRawString() {
        std::string result;
        ++pos_; // opening quote
        while (pos_ < input_.size()) {
            char c = input_[pos_++];
            if (c == '"') break;
            if (c == '\\' && pos_ < input_.size()) {
                const char escape = input_[pos_++];
                switch (escape) {
                case 'n': result.push_back('\n'); break;
                case 't': result.push_back('\t'); break;
                case 'r': result.push_back('\r'); break;
                default: result.push_back(escape); break;
                }
                continue;
            }
            result.push_back(c);
        }
        return result;
    }

    JsonValue parseBool() {
        JsonValue value;
        value.type = JsonValue::Type::Bool;
        value.boolean = peek() == 't';
        pos_ += value.boolean ? 4 : 5;
        return value;
    }

    JsonValue parseNumber() {
        JsonValue value;
        value.type = JsonValue::Type::Number;
        const std::size_t start = pos_;
        while (pos_ < input_.size()) {
            const char c = input_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') {
                ++pos_;
            } else {
                break;
            }
        }
        value.number = std::stod(std::string(input_.substr(start, pos_ - start)));
        return value;
    }
};

float floatField(const JsonValue* object, std::string_view key, float fallback) {
    if (object == nullptr) {
        return fallback;
    }
    const JsonValue* value = object->find(key);
    return value != nullptr ? static_cast<float>(value->numberOr(fallback)) : fallback;
}

int atlasSideFor(float size, float padding) {
    int side = 256;
    const double needed = 520.0 * (size + padding * 2.0) * (size + padding * 2.0);
    while (static_cast<double>(side) * side < needed && side < 4096) {
        side *= 2;
    }
    return side;
}

} // namespace

std::vector<std::uint8_t> readFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return {};
    }
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        return {};
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

std::string readFileText(const std::string& path) {
    const std::vector<std::uint8_t> bytes = readFileBytes(path);
    return std::string(bytes.begin(), bytes.end());
}

BakedFontAtlas bakeTtfAtlas(
        const std::vector<std::uint8_t>& fontBytes,
        float pixelSize,
        float oversample) {
    BakedFontAtlas result;
    if (fontBytes.empty() || pixelSize <= 0.0f) {
        return result;
    }

    const auto* data = reinterpret_cast<const unsigned char*>(fontBytes.data());
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0))) {
        return result;
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    const float vScale = stbtt_ScaleForMappingEmToPixels(&info, pixelSize);

    struct Range {
        int first;
        int last;
    };
    constexpr Range ranges[] = {
        {0x0020, 0x007E},
        {0x00A0, 0x00FF},
        {0x0400, 0x045F},
        {0x2010, 0x2027},
        {0x20A0, 0x20BF},
    };
    constexpr float padding = 2.0f;
    const int side = atlasSideFor(pixelSize, padding);

    int total = 0;
    for (const Range& range : ranges) {
        total += range.last - range.first + 1;
    }

    std::vector<std::uint8_t> bitmap(static_cast<std::size_t>(side) * side, 0);
    std::vector<stbtt_packedchar> packed(static_cast<std::size_t>(total));

    stbtt_pack_context context{};
    if (!stbtt_PackBegin(&context, bitmap.data(), side, side, 0, static_cast<int>(padding), nullptr)) {
        return result;
    }
    const int oversampleInt = static_cast<int>(std::max(1.0f, oversample));
    stbtt_PackSetOversampling(&context, static_cast<unsigned int>(oversampleInt),
                              static_cast<unsigned int>(oversampleInt));
    int offset = 0;
    for (const Range& range : ranges) {
        const int count = range.last - range.first + 1;
        // Negative pixel size selects em-to-pixel mapping (STBTT_POINT_SIZE).
        stbtt_PackFontRange(&context, data, 0, -pixelSize, range.first, count, packed.data() + offset);
        offset += count;
    }
    stbtt_PackEnd(&context);

    result.width = static_cast<std::uint32_t>(side);
    result.height = static_cast<std::uint32_t>(side);
    result.pixels.assign(static_cast<std::size_t>(side) * side * 4, 255);
    for (std::size_t index = 0; index < bitmap.size(); ++index) {
        result.pixels[index * 4 + 3] = bitmap[index];
    }

    result.atlas.mode = UiTextAtlasMode::TtfBitmap;
    result.atlas.emSize = pixelSize;
    result.atlas.ascender = 0.95f;
    result.atlas.lineHeight = pixelSize * 1.19f;
    result.atlas.distanceRange = 0.0f;
    result.atlas.glyphs.reserve(static_cast<std::size_t>(total));

    float penX = 0.0f;
    float penY = 0.0f;
    int glyphIndex = 0;
    for (const Range& range : ranges) {
        for (int cp = range.first; cp <= range.last; ++cp) {
            penX = 0.0f;
            penY = 0.0f;
            stbtt_aligned_quad quad{};
            stbtt_GetPackedQuad(packed.data(), side, side, glyphIndex, &penX, &penY, &quad, 0);
            ++glyphIndex;

            const float width = quad.x1 - quad.x0;
            const float height = quad.y1 - quad.y0;
            if (width <= 0.0f || height <= 0.0f) {
                continue;
            }
            UiGlyph glyph;
            glyph.codePoint = static_cast<std::uint32_t>(cp);
            glyph.u0 = quad.s0;
            glyph.v0 = quad.t0;
            glyph.u1 = quad.s1;
            glyph.v1 = quad.t1;
            glyph.planeLeft = quad.x0 / pixelSize;
            glyph.planeTop = -quad.y0 / pixelSize;
            glyph.planeWidth = width / pixelSize;
            glyph.planeHeight = height / pixelSize;
            glyph.advance = penX;
            result.atlas.glyphs.push_back(glyph);
        }
    }
    return result;
}

BakedFontAtlas loadMsdfAtlas(
        const std::string& json,
        const std::vector<std::uint8_t>& pngBytes) {
    BakedFontAtlas result;
    JsonParser parser(json);
    const JsonValue root = parser.parse();

    float atlasWidth = 512.0f;
    float atlasHeight = 512.0f;
    float fontSize = 32.0f;
    float distanceRange = 4.0f;
    bool yOriginBottom = false;
    if (const JsonValue* atlas = root.find("atlas")) {
        atlasWidth = floatField(atlas, "width", atlasWidth);
        atlasHeight = floatField(atlas, "height", atlasHeight);
        fontSize = floatField(atlas, "size", fontSize);
        distanceRange = floatField(atlas, "distanceRange", distanceRange);
        if (const JsonValue* origin = atlas->find("yOrigin")) {
            yOriginBottom = origin->type == JsonValue::Type::String && origin->text == "bottom";
        }
    }

    float lineHeight = fontSize * 1.2f;
    float ascender = 0.95f;
    if (const JsonValue* metrics = root.find("metrics")) {
        lineHeight = floatField(metrics, "lineHeight", 1.2f) * fontSize;
        ascender = floatField(metrics, "ascender", ascender);
    }

    result.atlas.mode = UiTextAtlasMode::Msdf;
    result.atlas.emSize = fontSize;
    result.atlas.lineHeight = lineHeight;
    result.atlas.ascender = ascender;
    result.atlas.distanceRange = distanceRange;

    if (const JsonValue* glyphs = root.find("glyphs"); glyphs != nullptr) {
        for (const JsonValue& entry : glyphs->array) {
            const JsonValue* unicode = entry.find("unicode");
            if (unicode == nullptr) {
                unicode = entry.find("id");
            }
            if (unicode == nullptr) {
                continue;
            }
            const auto codePoint = static_cast<std::uint32_t>(unicode->numberOr(0.0));

            float advance = floatField(&entry, "advance", 0.0f) * fontSize;
            if (advance == 0.0f) {
                advance = floatField(&entry, "xadvance", 0.0f);
            }

            const JsonValue* atlasBounds = entry.find("atlasBounds");
            if (atlasBounds == nullptr) {
                continue;
            }
            const float left = floatField(atlasBounds, "left", 0.0f);
            const float bottom = floatField(atlasBounds, "bottom", 0.0f);
            const float right = floatField(atlasBounds, "right", 0.0f);
            const float top = floatField(atlasBounds, "top", 0.0f);
            const float y = yOriginBottom ? atlasHeight - top : bottom;

            UiGlyph glyph;
            glyph.codePoint = codePoint;
            glyph.u0 = left / atlasWidth;
            glyph.v0 = y / atlasHeight;
            glyph.u1 = right / atlasWidth;
            glyph.v1 = (y + (top - bottom)) / atlasHeight;
            glyph.advance = advance;

            if (const JsonValue* planeBounds = entry.find("planeBounds")) {
                const float pLeft = floatField(planeBounds, "left", 0.0f);
                const float pTop = floatField(planeBounds, "top", 0.0f);
                const float pRight = floatField(planeBounds, "right", 0.0f);
                const float pBottom = floatField(planeBounds, "bottom", 0.0f);
                glyph.planeLeft = pLeft;
                glyph.planeTop = pTop;
                glyph.planeWidth = pRight - pLeft;
                glyph.planeHeight = pTop - pBottom;
            }
            result.atlas.glyphs.push_back(glyph);
        }
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    if (!pngBytes.empty()) {
        unsigned char* decoded = stbi_load_from_memory(
            pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, &channels, 4);
        if (decoded != nullptr) {
            result.width = static_cast<std::uint32_t>(width);
            result.height = static_cast<std::uint32_t>(height);
            result.pixels.assign(decoded, decoded + static_cast<std::size_t>(width) * height * 4);
            stbi_image_free(decoded);
        }
    }
    return result;
}

} // namespace Aporia::graphics_internal
