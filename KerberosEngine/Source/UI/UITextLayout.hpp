#pragma once

#include "Core/Core.hpp"
#include "Renderer/Font.hpp"
#include "UI/UITypes.hpp"
#include "Scene/Components/UIComponents.hpp"

#include <string>
#include <vector>

namespace Kerberos {

struct UITextStyle
{
    Ref<Font> Font = nullptr;
    uint32_t TextureIndex = 0;
    float FontSize = 20.0f;
    UITextAlign Align = UITextAlign::Left;
    glm::vec4 Color{ 1.0f };
    glm::vec4 ShadowColor{ 0.0f, 0.0f, 0.0f, 0.0f };
    glm::vec2 ShadowOffset{ 0.0f, 0.0f };
    /// 0 disables wrapping.
    float WrapWidth = 0.0f;
};

class UITextLayout
{
public:
    static float GetLineHeight(const Font& font, float fontSize);
    static float GetAscender(const Font& font, float fontSize);
    static float MeasureLineWidth(const Font& font, const std::string& line, float fontSize);

    /// Splits on explicit newlines and greedily wraps on spaces when maxWidth > 0.
    static std::vector<std::string> WrapLines(const Font& font, const std::string& text,
                                              float fontSize, float maxWidth);

    /// Appends MSDF glyph quads for text laid out inside rect. The block is
    /// vertically centered in rect (top-aligned when it does not fit) and
    /// horizontally aligned by style.Align. Glyphs outside clipRect are dropped.
    /// Returns the height used by the wrapped block.
    static float AppendText(UIDrawData& out, const UITextStyle& style, const std::string& text,
                            const UIRect& rect, const UIRect& clipRect, bool clipEnabled);
};

void AppendUIQuad(UIDrawData& data, const UIQuad& quad);

void AppendUIColorQuad(UIDrawData& data, 
                       const UIRect& rect, 
                       glm::vec4 color,
                       float radius = 0.0f,
                       UIDrawMode mode = UIDrawMode::Rect,
                       float borderWidth = 0.0f);

}
