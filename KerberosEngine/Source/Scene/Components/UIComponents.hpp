#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

#include "Assets/Asset.hpp"

namespace Kerberos
{
    /// Element kinds rendered by the game UI system.
    enum class UIElementType : uint8_t
    {
        Panel = 0,
        Text,
        Button,
        Toggle,
        Bar,
        TextInput,
        Image,
        Count
    };

    /// Screen-space canvas space or world-anchored screen space.
    enum class UISpace : uint8_t
    {
        Canvas = 0,
        World
    };

    /// Preset anchor points inside the parent rect. Stretch uses Margin instead of Size.
    enum class UIAnchor : uint8_t
    {
        TopLeft = 0,
        TopCenter,
        TopRight,
        MiddleLeft,
        Center,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        Stretch,
        Count
    };

    enum class UIGradientDirection : uint8_t
    {
        None = 0,
        Vertical,
        Horizontal,
        Count
    };

    enum class UITextAlign : uint8_t
    {
        Left = 0,
        Center,
        Right,
        Count
    };

    enum class UIFillDirection : uint8_t
    {
        LeftToRight = 0,
        RightToLeft,
        BottomToTop,
        TopToBottom,
        Count
    };

    enum class UIScaleMode : uint8_t
    {
        MatchHeight = 0,
        MatchWidth,
        None
    };

    /// Root scope for screen-space UI. 
    struct UICanvasComponent
    {
        bool Visible = true;
        // Canvases are painted in SortOrder order.
        int SortOrder = 0;
        /// When false the canvas renders but never reacts to pointer input.
        bool InputEnabled = true;
        /// While visible, exposes input capture through UISystem::IsInputCaptured.
        bool BlockSceneInput = false;
        UIScaleMode ScaleMode = UIScaleMode::MatchHeight;
        float ReferenceHeight = 1080.0f;
        float ReferenceWidth = 1920.0f;

        UICanvasComponent() = default;
        UICanvasComponent(const UICanvasComponent&) = default;
        UICanvasComponent(UICanvasComponent&&) = default;
        UICanvasComponent& operator=(const UICanvasComponent&) = default;
        UICanvasComponent& operator=(UICanvasComponent&&) = default;
    };

    /// One retained UI element. Type selects which field groups are used.
    struct UIElementComponent
    {
        UIElementType Type = UIElementType::Panel;
        UISpace Space = UISpace::Canvas;

        // ---- Layout ----
        UIAnchor Anchor = UIAnchor::TopLeft;
        glm::vec2 Pivot{ 0.0f, 0.0f };
        glm::vec2 Offset{ 0.0f, 0.0f };
        glm::vec2 Size{ 200.0f, 40.0f };
        /// Left, top, right, bottom insets used by UIAnchor::Stretch.
        glm::vec4 Margin{ 0.0f };
        int SortOrder = 0;

        // ---- Common ----
        bool Visible = true;
        bool Interactable = true;
        bool ClipChildren = false;
        float Opacity = 1.0f;

        // ---- Box style ----
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 GradientColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        UIGradientDirection GradientDirection = UIGradientDirection::None;
        glm::vec4 BorderColor{ 0.0f, 0.0f, 0.0f, 1.0f };
        float BorderWidth = 0.0f;
        float CornerRadius = 0.0f;
        AssetHandle Texture = AssetHandle::Invalid();

        // ---- Text ----
        std::string Text;
        std::string FontPath = "Fonts/Inter/Inter_18pt-Regular.ttf";
        float FontSize = 20.0f;
        UITextAlign TextAlign = UITextAlign::Center;
        /// 0 uses the element width for wrapping.
        float WrapWidth = 0.0f;
        glm::vec4 TextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 ShadowColor{ 0.0f, 0.0f, 0.0f, 0.5f };
        glm::vec2 ShadowOffset{ 0.0f, 0.0f };

        // ---- Interaction styling ----
        glm::vec4 HoverColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 PressColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 DisabledColor{ 1.0f, 1.0f, 1.0f, 1.0f };

        // ---- Bar ----
        /// Normalized fill amount in [0, 1].
        float Value = 1.0f;
        glm::vec4 FillColor{ 0.8f, 0.2f, 0.2f, 1.0f };
        glm::vec4 BackgroundColor{ 0.0f, 0.0f, 0.0f, 0.6f };
        UIFillDirection FillDirection = UIFillDirection::LeftToRight;

        // ---- Toggle ----
        bool IsOn = false;

        // ---- Text input ----
        std::string Placeholder;
        uint32_t MaxLength = 64;

        // ---- Transient runtime state (never serialized) ----
        bool IsHovered = false;
        bool IsPressed = false;
        bool IsFocused = false;
        /// True for one UI update after a press-release inside the element.
        bool Clicked = false;
        std::string InputBuffer;
        uint32_t CaretIndex = 0;

        UIElementComponent() = default;
        UIElementComponent(const UIElementComponent&) = default;
        UIElementComponent(UIElementComponent&&) = default;
        UIElementComponent& operator=(const UIElementComponent&) = default;
        UIElementComponent& operator=(UIElementComponent&&) = default;
    };

    // ------------------------------------------------------------------
    // String conversion helpers used by the scene serializer and inspector.
    // ------------------------------------------------------------------

    inline const char* ToString(const UIElementType value)
    {
        switch (value)
        {
        case UIElementType::Panel: return "Panel";
        case UIElementType::Text: return "Text";
        case UIElementType::Button: return "Button";
        case UIElementType::Toggle: return "Toggle";
        case UIElementType::Bar: return "Bar";
        case UIElementType::TextInput: return "TextInput";
        case UIElementType::Image: return "Image";
        default: return "Panel";
        }
    }

    inline const char* ToString(const UISpace value)
    {
        return value == UISpace::World ? "World" : "Canvas";
    }

    inline const char* ToString(const UIAnchor value)
    {
        switch (value)
        {
        case UIAnchor::TopLeft: return "TopLeft";
        case UIAnchor::TopCenter: return "TopCenter";
        case UIAnchor::TopRight: return "TopRight";
        case UIAnchor::MiddleLeft: return "MiddleLeft";
        case UIAnchor::Center: return "Center";
        case UIAnchor::MiddleRight: return "MiddleRight";
        case UIAnchor::BottomLeft: return "BottomLeft";
        case UIAnchor::BottomCenter: return "BottomCenter";
        case UIAnchor::BottomRight: return "BottomRight";
        case UIAnchor::Stretch: return "Stretch";
        default: return "TopLeft";
        }
    }

    inline const char* ToString(const UIGradientDirection value)
    {
        switch (value)
        {
        case UIGradientDirection::Vertical: return "Vertical";
        case UIGradientDirection::Horizontal: return "Horizontal";
        default: return "None";
        }
    }

    inline const char* ToString(const UITextAlign value)
    {
        switch (value)
        {
        case UITextAlign::Center: return "Center";
        case UITextAlign::Right: return "Right";
        default: return "Left";
        }
    }

    inline const char* ToString(const UIFillDirection value)
    {
        switch (value)
        {
        case UIFillDirection::RightToLeft: return "RightToLeft";
        case UIFillDirection::BottomToTop: return "BottomToTop";
        case UIFillDirection::TopToBottom: return "TopToBottom";
        default: return "LeftToRight";
        }
    }

    inline const char* ToString(const UIScaleMode value)
    {
        switch (value)
        {
        case UIScaleMode::MatchWidth: return "MatchWidth";
        case UIScaleMode::None: return "None";
        default: return "MatchHeight";
        }
    }

    inline UIElementType UIElementTypeFromString(const std::string& value)
    {
        if (value == "Text") return UIElementType::Text;
        if (value == "Button") return UIElementType::Button;
        if (value == "Toggle") return UIElementType::Toggle;
        if (value == "Bar") return UIElementType::Bar;
        if (value == "TextInput") return UIElementType::TextInput;
        if (value == "Image") return UIElementType::Image;
        return UIElementType::Panel;
    }

    inline UISpace UISpaceFromString(const std::string& value)
    {
        return value == "World" ? UISpace::World : UISpace::Canvas;
    }

    inline UIAnchor UIAnchorFromString(const std::string& value)
    {
        if (value == "TopCenter") return UIAnchor::TopCenter;
        if (value == "TopRight") return UIAnchor::TopRight;
        if (value == "MiddleLeft") return UIAnchor::MiddleLeft;
        if (value == "Center") return UIAnchor::Center;
        if (value == "MiddleRight") return UIAnchor::MiddleRight;
        if (value == "BottomLeft") return UIAnchor::BottomLeft;
        if (value == "BottomCenter") return UIAnchor::BottomCenter;
        if (value == "BottomRight") return UIAnchor::BottomRight;
        if (value == "Stretch") return UIAnchor::Stretch;
        return UIAnchor::TopLeft;
    }

    inline UIGradientDirection UIGradientDirectionFromString(const std::string& value)
    {
        if (value == "Vertical") return UIGradientDirection::Vertical;
        if (value == "Horizontal") return UIGradientDirection::Horizontal;
        return UIGradientDirection::None;
    }

    inline UITextAlign UITextAlignFromString(const std::string& value)
    {
        if (value == "Center") return UITextAlign::Center;
        if (value == "Right") return UITextAlign::Right;
        return UITextAlign::Left;
    }

    inline UIFillDirection UIFillDirectionFromString(const std::string& value)
    {
        if (value == "RightToLeft") return UIFillDirection::RightToLeft;
        if (value == "BottomToTop") return UIFillDirection::BottomToTop;
        if (value == "TopToBottom") return UIFillDirection::TopToBottom;
        return UIFillDirection::LeftToRight;
    }

    inline UIScaleMode UIScaleModeFromString(const std::string& value)
    {
        if (value == "MatchWidth") return UIScaleMode::MatchWidth;
        if (value == "None") return UIScaleMode::None;
        return UIScaleMode::MatchHeight;
    }
}
