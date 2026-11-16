#include "kbrpch.hpp"
#include "UISystem.hpp"

#include "Assets/AssetManager.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Scene/Components.hpp"
#include "UI/UIFontLibrary.hpp"
#include "UI/UITextLayout.hpp"

#include <algorithm>

namespace Kerberos {

namespace {
glm::vec2 AnchorPoint(const UIAnchor anchor)
{
    switch (anchor)
    {
    case UIAnchor::TopLeft: return { 0.0f, 0.0f };
    case UIAnchor::TopCenter: return { 0.5f, 0.0f };
    case UIAnchor::TopRight: return { 1.0f, 0.0f };
    case UIAnchor::MiddleLeft: return { 0.0f, 0.5f };
    case UIAnchor::Center: return { 0.5f, 0.5f };
    case UIAnchor::MiddleRight: return { 1.0f, 0.5f };
    case UIAnchor::BottomLeft: return { 0.0f, 1.0f };
    case UIAnchor::BottomCenter: return { 0.5f, 1.0f };
    case UIAnchor::BottomRight: return { 1.0f, 1.0f };
    default: return { 0.0f, 0.0f };
    }
}

float CanvasScale(const UICanvasComponent& canvas, const glm::vec2 screenSize)
{
    if (screenSize.x <= 0.0f || screenSize.y <= 0.0f)
        return 1.0f;

    switch (canvas.ScaleMode)
    {
    case UIScaleMode::MatchWidth:
        return canvas.ReferenceWidth > 0.0f ? std::max(screenSize.x / canvas.ReferenceWidth, 0.1f) : 1.0f;
    case UIScaleMode::None:
        return 1.0f;
    case UIScaleMode::MatchHeight:
    default:
        return canvas.ReferenceHeight > 0.0f ? std::max(screenSize.y / canvas.ReferenceHeight, 0.1f) : 1.0f;
    }
}

UIRect ComputeElementRect(const UIElementComponent& element, const UIRect& parentRect, const float scale)
{
    if (element.Anchor == UIAnchor::Stretch)
    {
        return {
            .Min = parentRect.Min + glm::vec2(element.Margin.x, element.Margin.y) * scale,
            .Max = parentRect.Max - glm::vec2(element.Margin.z, element.Margin.w) * scale
        };
    }

    const glm::vec2 anchor = AnchorPoint(element.Anchor);
    const glm::vec2 size = element.Size * scale;
    const glm::vec2 position = parentRect.Min + anchor * parentRect.Size() + element.Offset * scale -
                               element.Pivot * size;
    return { .Min = position, .Max = position + size };
}

UIRect IntersectRect(const UIRect& a, const UIRect& b)
{
    return { .Min = glm::max(a.Min, b.Min), .Max = glm::min(a.Max, b.Max) };
}

glm::vec4 TintColor(const glm::vec4& base, const glm::vec4& tint, const float opacity)
{
    glm::vec4 color = base * tint;
    color.a *= opacity;
    return glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f));
}

glm::vec4 ResolveInteractionTint(const UIElementComponent& element)
{
    if (element.Type != UIElementType::Button && element.Type != UIElementType::Toggle)
        return glm::vec4(1.0f);

    if (!element.Interactable)
        return element.DisabledColor;
    if (element.IsPressed)
        return element.PressColor;
    if (element.IsHovered)
        return element.HoverColor;

    return glm::vec4(1.0f);
}

glm::vec4 ResolveBoxColor(const UIElementComponent& element)
{
    return TintColor(element.Color, ResolveInteractionTint(element), element.Opacity);
}

UIQuad MakeBoxQuad(const UIRect& rect, const glm::vec4& color, const float radius,
                   const UIDrawMode mode = UIDrawMode::Rect, const float borderWidth = 0.0f)
{
    UIQuad quad;
    quad.Rect = rect;
    quad.Radius = radius;
    quad.Mode = mode;
    quad.BorderWidth = borderWidth;
    quad.ColorTopLeft = color;
    quad.ColorTopRight = color;
    quad.ColorBottomLeft = color;
    quad.ColorBottomRight = color;
    return quad;
}

UIQuad MakeGradientQuad(const UIRect& rect, const UIElementComponent& element, const glm::vec4& color,
                        const float radius)
{
    UIQuad quad = MakeBoxQuad(rect, color, radius);
    quad.Radius = radius;

    if (element.GradientDirection == UIGradientDirection::None)
        return quad;

    // The gradient half follows the same hover/press/disabled tint as the base
    // color, otherwise a gradient button only reacts on one side.
    const glm::vec4 second = TintColor(element.GradientColor, ResolveInteractionTint(element),
                                       element.Opacity);

    if (element.GradientDirection == UIGradientDirection::Vertical)
    {
        quad.ColorBottomLeft = second;
        quad.ColorBottomRight = second;
    }
    else
    {
        quad.ColorTopRight = second;
        quad.ColorBottomRight = second;
    }

    return quad;
}

UITextStyle MakeTextStyle(const UIElementComponent& element, const float scale)
{
    UITextStyle style;
    style.Font = UIFontLibrary::Get().Get(element.FontPath, element.FontSize * scale);
    style.TextureIndex = UIFontLibrary::Get().GetTextureIndex(style.Font);
    style.FontSize = element.FontSize * scale;
    style.Align = element.TextAlign;
    style.Color = glm::clamp(glm::vec4(element.TextColor.x, element.TextColor.y, element.TextColor.z,
                                       element.TextColor.a * element.Opacity),
                             glm::vec4(0.0f), glm::vec4(1.0f));
    style.ShadowColor = element.ShadowColor;
    style.ShadowColor.a *= element.Opacity;
    style.ShadowOffset = element.ShadowOffset * scale;
    style.WrapWidth = element.WrapWidth > 0.0f ? element.WrapWidth * scale : 0.0f;
    return style;
}

bool IsTextElement(const UIElementComponent& element)
{
    return element.Type == UIElementType::Text || element.Type == UIElementType::Button ||
           element.Type == UIElementType::Toggle || element.Type == UIElementType::TextInput;
}

}

UIElementComponent* UISystem::TryGetElement(Scene& scene, const entt::entity entity)
{
	if (entity == entt::null)
		return nullptr;

	const Entity handle{ entity, &scene };
	if (!scene.IsEntityValid(handle) || !handle.HasComponent<UIElementComponent>())
		return nullptr;

	return &handle.GetComponent<UIElementComponent>();
}

void UISystem::ResolveElements(Scene& scene, const glm::vec2 screenSize, const glm::mat4& viewProjection)
{
	m_Elements.clear();

	const UIRect screenRect{ .Min = { 0.0f, 0.0f }, .Max = screenSize };
	std::unordered_set<uint32_t> visited;

	// ---- World-space elements (painted first, farthest first) ----
	{
		const auto view = scene.m_Registry.view<UIElementComponent, TransformComponent>();
		for (const auto entityID : view)
		{
			auto& element = view.get<UIElementComponent>(entityID);
			if (element.Space != UISpace::World || !element.Visible)
				continue;

			UICanvasComponent* canvas = nullptr;
			Entity parent = scene.GetParent(Entity{ entityID, &scene });
			while (parent)
			{
				if (parent.HasComponent<UICanvasComponent>())
				{
					canvas = &parent.GetComponent<UICanvasComponent>();
					break;
				}
				parent = scene.GetParent(parent);
			}

			if (canvas && !canvas->Visible)
				continue;

			const auto& transform = view.get<TransformComponent>(entityID);
			const glm::vec3 worldPosition{ transform.WorldTransform[3] };
			const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
			if (clip.w <= 0.0001f)
				continue;

			const glm::vec2 ndc{ clip.x / clip.w, clip.y / clip.w };
			const glm::vec2 screenPosition{ (ndc.x * 0.5f + 0.5f) * screenSize.x,
				                            (0.5f - ndc.y * 0.5f) * screenSize.y };
			const glm::vec2 size = element.Size;
			const glm::vec2 position = screenPosition + element.Offset - element.Pivot * size;

			ResolvedElement resolved;
			resolved.Entity = entityID;
			resolved.Element = &element;
			resolved.Canvas = canvas;
			resolved.CanvasSortOrder = canvas ? canvas->SortOrder : 0;
			resolved.Order = 0;
			resolved.WorldSpace = true;
			resolved.WorldDepth = clip.w;
			resolved.Rect = { .Min = position, .Max = position + size };
			resolved.ClipRect = screenRect;
			resolved.ClipEnabled = false;
			resolved.Scale = 1.0f;
			m_Elements.push_back(resolved);
			visited.insert(static_cast<uint32_t>(entityID));
		}
	}

	// ---- Screen-space canvases ----
	std::vector<entt::entity> canvasEntities;
	{
		const auto view = scene.m_Registry.view<UICanvasComponent>();
		canvasEntities.assign(view.begin(), view.end());
	}

	std::ranges::stable_sort(canvasEntities, [&](const entt::entity a, const entt::entity b)
    {
        return scene.m_Registry.get<UICanvasComponent>(a).SortOrder <
               scene.m_Registry.get<UICanvasComponent>(b).SortOrder;
    });

	for (const entt::entity canvasID : canvasEntities)
	{
		if (visited.contains(static_cast<uint32_t>(canvasID)))
			continue;

		auto& canvas = scene.m_Registry.get<UICanvasComponent>(canvasID);
		if (!canvas.Visible)
			continue;

		const float scale = CanvasScale(canvas, screenSize);
		int order = 0;

		// A canvas entity may carry an element of its own (full-screen backdrop).
		LayoutEntity(scene, canvasID, &canvas, canvas.SortOrder, scale, screenRect, screenRect,
		             false, order, visited);
	}

	// ---- Elements without a canvas ancestor fall back to a default screen canvas ----
	{
		const float scale = screenSize.y > 0.0f ? screenSize.y / 1080.0f : 1.0f;
		const auto view = scene.m_Registry.view<UIElementComponent>();
		int order = 1000000;
		for (const auto entityID : view)
		{
			if (visited.contains(static_cast<uint32_t>(entityID)))
				continue;

			auto& element = view.get<UIElementComponent>(entityID);
			if (element.Space != UISpace::Canvas)
				continue;
			if (!element.Visible)
				continue;

			ResolvedElement resolved;
			resolved.Entity = entityID;
			resolved.Element = &element;
			resolved.Canvas = nullptr;
			resolved.CanvasSortOrder = 0;
			resolved.Order = order++;
			resolved.Rect = ComputeElementRect(element, screenRect, scale);
			resolved.ClipRect = screenRect;
			resolved.ClipEnabled = false;
			resolved.Scale = scale;
			m_Elements.push_back(resolved);
		}
	}

	std::ranges::stable_sort(m_Elements, [](const ResolvedElement& a, const ResolvedElement& b)
    {
        if (a.WorldSpace != b.WorldSpace)
            return a.WorldSpace;
        if (a.WorldSpace)
            return a.WorldDepth > b.WorldDepth;
        if (a.CanvasSortOrder != b.CanvasSortOrder)
            return a.CanvasSortOrder < b.CanvasSortOrder;
        return a.Order < b.Order;
    });
}

void UISystem::LayoutEntity(Scene& scene, const entt::entity entityID, UICanvasComponent* canvas,
                            const int canvasSortOrder, const float scale, const UIRect& parentRect,
                            const UIRect& parentClip, const bool parentClipEnabled, int& order,
                            std::unordered_set<uint32_t>& visited)
{
	const Entity entity{ entityID, &scene };
	if (!entity)
		return;

	UIRect childParentRect = parentRect;
	UIRect childParentClip = parentClip;
	bool childClipEnabled = parentClipEnabled;

	if (entity.HasComponent<UIElementComponent>())
	{
		auto& element = entity.GetComponent<UIElementComponent>();

		if (element.Space == UISpace::World)
			return; // handled by the world pass

		if (!element.Visible)
			return; // hidden parents hide their subtree

		ResolvedElement resolved;
		resolved.Entity = entityID;
		resolved.Element = &element;
		resolved.Canvas = canvas;
		resolved.CanvasSortOrder = canvasSortOrder;
		resolved.Order = order++;
		resolved.WorldSpace = false;
		resolved.Rect = ComputeElementRect(element, parentRect, scale);
		resolved.ClipRect = parentClip;
		resolved.ClipEnabled = parentClipEnabled;
		resolved.Scale = scale;
		m_Elements.push_back(resolved);
		visited.insert(static_cast<uint32_t>(entityID));

		childParentRect = resolved.Rect;
		if (element.ClipChildren)
		{
			childParentClip = IntersectRect(parentClip, resolved.Rect);
			childClipEnabled = true;
		}
	}

	for (const Entity child : scene.GetChildren(entity))
	{
		LayoutEntity(scene, static_cast<entt::entity>(child), canvas, canvasSortOrder, scale,
		             childParentRect, childParentClip, childClipEnabled, order, visited);
	}
}

void UISystem::Update(Scene& scene, const glm::vec2 screenSize, const glm::mat4& viewProjection,
                      const UIInputState& input, const bool interact)
{
	KBR_TRACY_FUNCTION();

	ResolveElements(scene, screenSize, viewProjection);

	// Warm font atlases and image textures here, not during command recording: a
	// first-use font uploads its atlas and a first-use texture loads from disk,
	// and neither may create a one-time command buffer while the frame is being
	// recorded.
	for (const ResolvedElement& resolved : m_Elements)
	{
		const UIElementComponent& element = *resolved.Element;

		if (IsTextElement(element))
		{
			UIFontLibrary::Get().Get(element.FontPath, element.FontSize * resolved.Scale);
			continue;
		}

		if (element.Type == UIElementType::Image && element.Texture.IsValid())
			AssetManager::GetAsset<Texture2D>(element.Texture);
	}

	for (const auto entityID : scene.m_Registry.view<UIElementComponent>())
	{
		UIElementComponent& element = scene.m_Registry.get<UIElementComponent>(entityID);
		element.IsHovered = false;
		element.IsPressed = false;
		element.Clicked = false;
		element.IsFocused = false;
	}

	// Clear focus when the focused entity no longer resolves.
	if (m_FocusedEntity != entt::null)
	{
		const bool found = std::ranges::any_of(m_Elements,
                                               [&](const ResolvedElement& r) { return r.Entity == m_FocusedEntity; });
		if (!found)
			m_FocusedEntity = entt::null;
	}

	m_InputCaptured = false;

	// A visible modal canvas captures input even when it owns no elements of its
	// own: an empty pause-screen backdrop must still suppress gameplay input.
	for (const auto canvasID : scene.m_Registry.view<UICanvasComponent>())
	{
		const UICanvasComponent& canvas = scene.m_Registry.get<UICanvasComponent>(canvasID);
		if (canvas.Visible && canvas.InputEnabled && canvas.BlockSceneInput)
		{
			m_InputCaptured = true;
			break;
		}
	}

	if (!interact)
	{
		m_PressedEntity = entt::null;
		m_PreviousInput = input;
		return;
	}

	// ---- Hover (topmost first) ----
	const ResolvedElement* hovered = nullptr;
	if (input.MouseInsideViewport)
	{
		for (auto it = m_Elements.rbegin(); it != m_Elements.rend(); ++it)
		{
			ResolvedElement& candidate = *it;
			if (!candidate.Element->Visible || !candidate.Element->Interactable)
				continue;
			if (candidate.Canvas && (!candidate.Canvas->Visible || !candidate.Canvas->InputEnabled))
				continue;

			if (candidate.ClipEnabled)
			{
				const UIRect clipped = IntersectRect(candidate.Rect, candidate.ClipRect);
				if (!clipped.Contains(input.MousePosition))
					continue;
			}
			else if (!candidate.Rect.Contains(input.MousePosition))
			{
				continue;
			}

			hovered = &candidate;
			break;
		}
	}

	if (hovered)
		hovered->Element->IsHovered = true;

	const bool pressedThisFrame =
	    input.LeftPressed || (input.LeftDown && !m_PreviousInput.LeftDown);
	const bool releasedThisFrame =
	    input.LeftReleased || (!input.LeftDown && m_PreviousInput.LeftDown);

	if (pressedThisFrame)
	{
		m_PressedEntity = hovered ? hovered->Entity : entt::null;

		if (hovered)
		{
			UIElementComponent& element = *hovered->Element;
			if (element.Type == UIElementType::TextInput)
			{
				m_FocusedEntity = hovered->Entity;
				element.CaretIndex = static_cast<uint32_t>(element.InputBuffer.size());
			}
			else
			{
				m_FocusedEntity = entt::null;
			}
		}
		else
		{
			m_FocusedEntity = entt::null;
		}
	}

	if (input.LeftDown && m_PressedEntity != entt::null)
	{
		if (UIElementComponent* pressed = TryGetElement(scene, m_PressedEntity))
			pressed->IsPressed = true;
	}

	if (releasedThisFrame)
	{
		if (m_PressedEntity != entt::null && hovered && hovered->Entity == m_PressedEntity)
		{
			UIElementComponent& element = *hovered->Element;
			element.Clicked = true;

			if (element.Type == UIElementType::Toggle)
				element.IsOn = !element.IsOn;

			if (element.Type == UIElementType::Bar)
			{
				const glm::vec2 size = hovered->Rect.Size();
				const glm::vec2 local = input.MousePosition - hovered->Rect.Min;
				float value = 0.0f;
				switch (element.FillDirection)
				{
				case UIFillDirection::RightToLeft:
					value = size.x > 0.0f ? 1.0f - local.x / size.x : 0.0f;
					break;
				case UIFillDirection::BottomToTop:
					value = size.y > 0.0f ? 1.0f - local.y / size.y : 0.0f;
					break;
				case UIFillDirection::TopToBottom:
					value = size.y > 0.0f ? local.y / size.y : 0.0f;
					break;
				case UIFillDirection::LeftToRight:
				default:
					value = size.x > 0.0f ? local.x / size.x : 0.0f;
					break;
				}
				element.Value = glm::clamp(value, 0.0f, 1.0f);
			}
		}

		m_PressedEntity = entt::null;
	}

	// ---- Text input on the focused element ----
	if (m_FocusedEntity != entt::null)
	{
		if (UIElementComponent* focused = TryGetElement(scene, m_FocusedEntity);
		    focused && focused->Type == UIElementType::TextInput && focused->Visible)
		{
			focused->IsFocused = true;
			m_InputCaptured = true;

			if (input.Escape || input.Enter || input.Tab)
			{
				m_FocusedEntity = entt::null;
			}
			else
			{
				focused->CaretIndex = std::min<uint32_t>(
					focused->CaretIndex, static_cast<uint32_t>(focused->InputBuffer.size()));

				if (input.Backspace && focused->CaretIndex > 0)
				{
					focused->InputBuffer.erase(focused->CaretIndex - 1, 1);
					focused->CaretIndex--;
				}

				for (const char character : input.TypedCharacters)
				{
					if (character < 32 || character == 127)
						continue;
					if (focused->MaxLength > 0 && focused->InputBuffer.size() >= focused->MaxLength)
						break;
					focused->InputBuffer.insert(focused->CaretIndex, 1, character);
					focused->CaretIndex++;
				}
			}
		}
		else
		{
			m_FocusedEntity = entt::null;
		}
	}

	m_PreviousInput = input;
}

void UISystem::AppendElementDrawData(const ResolvedElement& resolved, 
                                     UIDrawData& out,
                                     const std::function<uint32_t(const AssetHandle&)>& resolveTexture)
{
	const UIElementComponent& element = *resolved.Element;
	const UIRect& rect = resolved.Rect;
	const float scale = resolved.Scale;
	const float radius = element.CornerRadius * scale;
	const float borderWidth = element.BorderWidth * scale;
	const glm::vec4 boxColor = ResolveBoxColor(element);

	switch (element.Type)
	{
	case UIElementType::Panel:
	{
		AppendUIQuad(out, MakeGradientQuad(rect, element, boxColor, radius));
		if (borderWidth > 0.0f)
		{
			glm::vec4 border = element.BorderColor;
			border.a *= element.Opacity;
			AppendUIQuad(out, MakeBoxQuad(rect, border, radius, UIDrawMode::RectBorder, borderWidth));
		}
		break;
	}
	case UIElementType::Image:
	{
		const uint32_t textureIndex = element.Texture.IsValid() && resolveTexture
		                                  ? resolveTexture(element.Texture)
		                                  : 0;
		if (textureIndex == 0)
			break;

		UIQuad quad = MakeBoxQuad(rect, boxColor, radius);
		quad.Mode = UIDrawMode::Image;
		quad.TextureIndex = textureIndex;
		AppendUIQuad(out, quad);
		break;
	}
	case UIElementType::Bar:
	{
		glm::vec4 background = element.BackgroundColor;
		background.a *= element.Opacity;
		AppendUIQuad(out, MakeBoxQuad(rect, background, radius));

		const float value = glm::clamp(element.Value, 0.0f, 1.0f);
		UIRect fill = rect;
		switch (element.FillDirection)
		{
		case UIFillDirection::RightToLeft:
			fill.Min.x = rect.Max.x - rect.Size().x * value;
			break;
		case UIFillDirection::BottomToTop:
			fill.Min.y = rect.Max.y - rect.Size().y * value;
			break;
		case UIFillDirection::TopToBottom:
			fill.Max.y = rect.Min.y + rect.Size().y * value;
			break;
		case UIFillDirection::LeftToRight:
		default:
			fill.Max.x = rect.Min.x + rect.Size().x * value;
			break;
		}

		glm::vec4 fillColor = element.FillColor;
		fillColor.a *= element.Opacity;
		fillColor = glm::clamp(fillColor, glm::vec4(0.0f), glm::vec4(1.0f));
		if (fill.IsValid())
			AppendUIQuad(out, MakeBoxQuad(fill, fillColor, radius));

		if (borderWidth > 0.0f)
		{
			glm::vec4 border = element.BorderColor;
			border.a *= element.Opacity;
			AppendUIQuad(out, MakeBoxQuad(rect, border, radius, UIDrawMode::RectBorder, borderWidth));
		}
		break;
	}
	case UIElementType::Toggle:
	{
		AppendUIQuad(out, MakeGradientQuad(rect, element, boxColor, radius));

		const float inset = glm::max(2.0f * scale, radius * 0.25f);
		const float knobSize = glm::max(rect.Size().y - inset * 2.0f, 1.0f);
		glm::vec2 knobMin;
		if (element.IsOn)
			knobMin = { rect.Max.x - inset - knobSize, rect.Min.y + inset };
		else
			knobMin = { rect.Min.x + inset, rect.Min.y + inset };

		glm::vec4 knobColor{ 0.95f, 0.95f, 0.95f, element.Opacity };
		AppendUIQuad(out, MakeBoxQuad({ knobMin, knobMin + glm::vec2(knobSize) },
		                             glm::clamp(knobColor, glm::vec4(0.0f), glm::vec4(1.0f)),
		                             knobSize * 0.5f));

		if (!element.Text.empty())
		{
			UITextStyle style = MakeTextStyle(element, scale);
			style.Align = UITextAlign::Center;
			const UIRect textRect{ rect.Min + glm::vec2(8.0f * scale, 0.0f),
				                   rect.Max - glm::vec2(knobSize + 12.0f * scale, 0.0f) };
			UITextLayout::AppendText(out, style, element.Text, textRect, rect, true);
		}

		if (borderWidth > 0.0f)
		{
			glm::vec4 border = element.BorderColor;
			border.a *= element.Opacity;
			AppendUIQuad(out, MakeBoxQuad(rect, border, radius, UIDrawMode::RectBorder, borderWidth));
		}
		break;
	}
	case UIElementType::Button:
	{
		AppendUIQuad(out, MakeGradientQuad(rect, element, boxColor, radius));

		if (borderWidth > 0.0f)
		{
			glm::vec4 border = element.BorderColor;
			border.a *= element.Opacity;
			AppendUIQuad(out, MakeBoxQuad(rect, border, radius, UIDrawMode::RectBorder, borderWidth));
		}

		if (!element.Text.empty())
		{
			UITextStyle style = MakeTextStyle(element, scale);
			style.Align = UITextAlign::Center;
			const UIRect textRect{ rect.Min + glm::vec2(6.0f * scale), rect.Max - glm::vec2(6.0f * scale) };
			UITextLayout::AppendText(out, style, element.Text, textRect, rect, true);
		}
		break;
	}
	case UIElementType::Text:
	{
		if (!element.Text.empty())
		{
			UITextStyle style = MakeTextStyle(element, scale);
			UITextLayout::AppendText(out, style, element.Text, rect, rect, false);
		}
		break;
	}
	case UIElementType::TextInput:
	{
		glm::vec4 background = element.BackgroundColor;
		background.a *= element.Opacity;
		AppendUIQuad(out, MakeBoxQuad(rect, background, radius));

		glm::vec4 border = element.IsFocused
		                       ? glm::vec4(element.BorderColor * element.HoverColor)
		                       : element.BorderColor;
		border.a *= element.Opacity;
		if (borderWidth > 0.0f)
			AppendUIQuad(out, MakeBoxQuad(rect, border, radius, UIDrawMode::RectBorder, borderWidth));

		UITextStyle style = MakeTextStyle(element, scale);
		style.Align = UITextAlign::Left;

		const bool showPlaceholder = element.InputBuffer.empty();
		const std::string& content = showPlaceholder ? element.Placeholder : element.InputBuffer;
		if (showPlaceholder)
		{
			style.Color.a *= 0.45f;
		}

		const UIRect textRect{ rect.Min + glm::vec2(8.0f * scale, 0.0f),
			                   rect.Max - glm::vec2(8.0f * scale, 0.0f) };
		UITextLayout::AppendText(out, style, content, textRect, rect, true);

		if (element.IsFocused && !showPlaceholder && style.Font)
		{
			const std::string prefix = element.InputBuffer.substr(0, element.CaretIndex);
			const float caretX = textRect.Min.x +
			                     UITextLayout::MeasureLineWidth(*style.Font, prefix, style.FontSize);
			const float caretHeight = UITextLayout::GetAscender(*style.Font, style.FontSize) * 0.9f;
			const float caretY = rect.Center().y - caretHeight * 0.5f;
			glm::vec4 caretColor{ element.TextColor.x, element.TextColor.y, element.TextColor.z,
				                  element.Opacity };
			AppendUIQuad(out, MakeBoxQuad({ .Min = { caretX, caretY },
                                            .Max = { caretX + glm::max(scale, 1.0f), caretY + caretHeight } },
			                             glm::clamp(caretColor, glm::vec4(0.0f), glm::vec4(1.0f)), 0.0f));
		}
		break;
	}
	default:
		break;
	}
}

const UIDrawData& UISystem::BuildDrawData(Scene& scene, const glm::vec2 screenSize,
                                          const glm::mat4& viewProjection,
                                          const std::function<uint32_t(const AssetHandle&)>& resolveTexture)
{
	KBR_TRACY_FUNCTION();

	ResolveElements(scene, screenSize, viewProjection);

	m_DrawData.Clear();
	if (m_DrawData.Vertices.capacity() < 4096)
	{
		m_DrawData.Vertices.reserve(4096);
		m_DrawData.Indices.reserve(8192);
	}

	for (const ResolvedElement& resolved : m_Elements)
		AppendElementDrawData(resolved, m_DrawData, resolveTexture);

	return m_DrawData;
}

void UISystem::ResetTransientState(Scene& scene)
{
	const auto view = scene.m_Registry.view<UIElementComponent>();
	for (const auto entityID : view)
	{
		auto& element = view.get<UIElementComponent>(entityID);
		element.IsHovered = false;
		element.IsPressed = false;
		element.IsFocused = false;
		element.Clicked = false;
		element.InputBuffer.clear();
		element.CaretIndex = 0;
	}

	m_PressedEntity = entt::null;
	m_FocusedEntity = entt::null;
	m_InputCaptured = false;
	m_Elements.clear();
	m_DrawData.Clear();
}

}
