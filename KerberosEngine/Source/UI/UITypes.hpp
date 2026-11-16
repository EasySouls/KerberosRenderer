#pragma once

#include "Renderer/Vertex.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace Kerberos {

/// Axis-aligned rectangle in output pixels, top-left origin.
struct UIRect
{
    glm::vec2 Min{ 0.0f };
    glm::vec2 Max{ 0.0f };

    glm::vec2 Size() const { return Max - Min; }
    glm::vec2 Center() const { return (Min + Max) * 0.5f; }

    bool Contains(const glm::vec2 point) const
    {
        return point.x >= Min.x && point.x <= Max.x && point.y >= Min.y && point.y <= Max.y;
    }

    bool Overlaps(const UIRect& other) const
    {
        return Min.x < other.Max.x && Max.x > other.Min.x && Min.y < other.Max.y && Max.y > other.Min.y;
    }

    bool IsValid() const { return Max.x > Min.x && Max.y > Min.y; }

    static UIRect FromPositionSize(const glm::vec2 position, const glm::vec2 size)
    {
        return { position, position + size };
    }
};

/// Per-frame input snapshot supplied by the host (editor Play mode or a game layer).
struct UIInputState
{
    /// Mouse position in output pixels, top-left origin.
    glm::vec2 MousePosition{ 0.0f };
    bool MouseInsideViewport = false;
    bool LeftDown = false;
    bool RightDown = false;

    /// Characters typed this frame (already filtered for text input).
    std::string TypedCharacters;
    bool Backspace = false;
    bool Enter = false;
    bool Tab = false;
    bool Escape = false;
};

enum class UIDrawMode : uint32_t
{
    Rect = 0,
    Text = 1,
    Image = 2,
    RectBorder = 3
};

struct UIDrawData
{
    std::vector<UIVertex> Vertices;
    std::vector<uint32_t> Indices;

    void Clear()
    {
        Vertices.clear();
        Indices.clear();
    }

    bool Empty() const { return Vertices.empty() || Indices.empty(); }
};

/// Axis-aligned quad with per-corner colors (gradients) and optional rounded corners.
struct UIQuad
{
    UIRect Rect{};
    glm::vec2 UVMin{ 0.0f, 0.0f };
    glm::vec2 UVMax{ 1.0f, 1.0f };
    glm::vec4 ColorTopLeft{ 1.0f };
    glm::vec4 ColorTopRight{ 1.0f };
    glm::vec4 ColorBottomLeft{ 1.0f };
    glm::vec4 ColorBottomRight{ 1.0f };
    uint32_t TextureIndex = 0;
    UIDrawMode Mode = UIDrawMode::Rect;
    float Radius = 0.0f;
    float BorderWidth = 0.0f;
};

}
