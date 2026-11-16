#include "kbrpch.hpp"
#include "UITextLayout.hpp"

#include <algorithm>

namespace Kerberos {

namespace {

bool IsWhitespace(const char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

double AdvanceWithKerning(const Font& font, const char current, const char next)
{
    KBR_TRACY_FUNCTION();
    
	if (!font.HasCharacter(current))
		return 0.0;

	if (font.HasCharacter(next))
	{
		double advance = font.GetAdvance(current);
		font.GetNextAdvance(advance, current, next);
		return advance;
	}

	return font.GetAdvance(current);
}

}

float UITextLayout::GetLineHeight(const Font& font, const float fontSize)
{
	const FontMetrics metrics = font.GetMetrics();
	return static_cast<float>(metrics.LineHeight) * fontSize;
}

float UITextLayout::GetAscender(const Font& font, const float fontSize)
{
	return static_cast<float>(font.GetMetrics().Ascender) * fontSize;
}

float UITextLayout::MeasureLineWidth(const Font& font, const std::string& line, const float fontSize)
{
    KBR_TRACY_FUNCTION();
    
	double width = 0.0;
	for (size_t i = 0; i < line.size(); ++i)
	{
		const char current = line[i];
		const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';
		width += AdvanceWithKerning(font, current, next);
	}

	return static_cast<float>(width) * fontSize;
}

std::vector<std::string> UITextLayout::WrapLines(const Font& font, const std::string& text,
                                                 const float fontSize, const float maxWidth)
{
    KBR_TRACY_FUNCTION();
    
	std::vector<std::string> lines;
	std::string paragraph;
	paragraph.reserve(text.size());

	const auto flushParagraph = [&](std::string value)
	{
		if (maxWidth <= 0.0f)
		{
			lines.push_back(std::move(value));
			return;
		}

		std::string line;
		std::string word;
		const auto pushWord = [&](const std::string& candidate)
		{
			if (candidate.empty())
				return;

			std::string probe = line;
			if (!probe.empty())
				probe += ' ';
			probe += candidate;

			if (MeasureLineWidth(font, probe, fontSize) <= maxWidth || line.empty())
			{
				line = std::move(probe);
				return;
			}

			lines.push_back(line);
			line = candidate;
		};

		for (const char c : value)
		{
			if (IsWhitespace(c))
			{
				pushWord(word);
				word.clear();
			}
			else
			{
				word += c;
			}
		}

		pushWord(word);
		lines.push_back(line);
	};

	for (size_t i = 0; i < text.size(); ++i)
	{
		const char c = text[i];
		if (c == '\n')
		{
			flushParagraph(paragraph);
			paragraph.clear();
		}
		else
		{
			paragraph += c;
		}
	}

	flushParagraph(paragraph);

	if (lines.empty())
		lines.emplace_back();

	return lines;
}

float UITextLayout::AppendText(UIDrawData& out, const UITextStyle& style, const std::string& text,
                               const UIRect& rect, const UIRect& clipRect, const bool clipEnabled)
{
    KBR_TRACY_FUNCTION();
    
	if (!style.Font || text.empty() || style.FontSize <= 0.0f)
		return 0.0f;

	const Font& font = *style.Font;
	const auto* atlas = font.GetAtlasTexture().get();
	if (!atlas || atlas->width == 0 || atlas->height == 0)
		return 0.0f;

	const float atlasWidth = static_cast<float>(atlas->width);
	const float atlasHeight = static_cast<float>(atlas->height);

	const float fontSize = style.FontSize;
	const float lineHeight = GetLineHeight(font, fontSize);
	const float ascender = GetAscender(font, fontSize);

	const float wrapWidth = style.WrapWidth > 0.0f ? style.WrapWidth : rect.Size().x;
	const std::vector<std::string> lines = WrapLines(font, text, fontSize, wrapWidth);

	float baselineY = rect.Min.y + ascender;
	float usedHeight = 0.0f;

	for (const std::string& line : lines)
	{
		const float lineWidth = MeasureLineWidth(font, line, fontSize);

		float penX = rect.Min.x;
		switch (style.Align)
		{
		case UITextAlign::Center: penX = rect.Center().x - lineWidth * 0.5f; break;
		case UITextAlign::Right: penX = rect.Max.x - lineWidth; break;
		default: break;
		}

		for (size_t i = 0; i < line.size(); ++i)
		{
			const char current = line[i];
			const char next = (i + 1 < line.size()) ? line[i + 1] : '\0';

			if (!font.HasCharacter(current))
			{
				continue;
			}

			double pl = 0.0, pb = 0.0, pr = 0.0, pt = 0.0;
			font.GetQuadPlaneBounds(current, pl, pb, pr, pt);

			const double advance = AdvanceWithKerning(font, current, next);
			const float advancePx = static_cast<float>(advance) * fontSize;

			const float glyphWidth = static_cast<float>(pr - pl) * fontSize;
			const float glyphHeight = static_cast<float>(pt - pb) * fontSize;

			if (glyphWidth > 0.0f && glyphHeight > 0.0f)
			{
				double al = 0.0, ab = 0.0, ar = 0.0, at = 0.0;
				font.GetQuadAtlasBounds(current, al, ab, ar, at);

				// The atlas is stored y-up in msdf-atlas-gen and uploaded top-down,
				// so the top atlas bound maps to the smaller Vulkan V coordinate.
				UIQuad quad;
				quad.Rect = {
                    .Min = { penX + static_cast<float>(pl) * fontSize, baselineY - static_cast<float>(pt) * fontSize },
                    .Max = { penX + static_cast<float>(pr) * fontSize, baselineY - static_cast<float>(pb) * fontSize }
                };
				quad.UVMin = { static_cast<float>(al) / atlasWidth, 1.0f - static_cast<float>(at) / atlasHeight };
				quad.UVMax = { static_cast<float>(ar) / atlasWidth, 1.0f - static_cast<float>(ab) / atlasHeight };
				quad.TextureIndex = style.TextureIndex;
				quad.Mode = UIDrawMode::Text;

				const bool inside = !clipEnabled || quad.Rect.Overlaps(clipRect);
				if (inside)
				{
					if (style.ShadowOffset != glm::vec2(0.0f) && style.ShadowColor.a > 0.0f)
					{
						UIQuad shadow = quad;
						shadow.Rect.Min += style.ShadowOffset;
						shadow.Rect.Max += style.ShadowOffset;
						shadow.ColorTopLeft = style.ShadowColor;
						shadow.ColorTopRight = style.ShadowColor;
						shadow.ColorBottomLeft = style.ShadowColor;
						shadow.ColorBottomRight = style.ShadowColor;
						AppendUIQuad(out, shadow);
					}

					quad.ColorTopLeft = style.Color;
					quad.ColorTopRight = style.Color;
					quad.ColorBottomLeft = style.Color;
					quad.ColorBottomRight = style.Color;
					AppendUIQuad(out, quad);
				}
			}

			penX += advancePx;
		}

		baselineY += lineHeight;
		usedHeight += lineHeight;
	}

	return usedHeight;
}


void AppendUIQuad(UIDrawData& data, const UIQuad& quad)
{
    KBR_TRACY_FUNCTION();
    
    if (!quad.Rect.IsValid())
        return;

    const auto base = static_cast<uint32_t>(data.Vertices.size());
    const glm::vec2 size = quad.Rect.Size();
    const glm::vec4 style{ size.x * 0.5f, size.y * 0.5f, quad.Radius, quad.BorderWidth };
    const auto mode = static_cast<uint32_t>(quad.Mode);

    data.Vertices.push_back({ .Position = quad.Rect.Min,
                              .UV = quad.UVMin,
                              .Color = quad.ColorTopLeft,
                              .LocalUV = { 0.0f, 0.0f },
                              .Style = style,
                              .TextureIndex = quad.TextureIndex,
                              .Mode = mode });
    data.Vertices.push_back({ .Position = { quad.Rect.Max.x, quad.Rect.Min.y },
                              .UV = { quad.UVMax.x, quad.UVMin.y },
                              .Color = quad.ColorTopRight,
                              .LocalUV = { 1.0f, 0.0f },
                              .Style = style,
                              .TextureIndex = quad.TextureIndex,
                              .Mode = mode });
    data.Vertices.push_back({ .Position = quad.Rect.Max,
                              .UV = quad.UVMax,
                              .Color = quad.ColorBottomRight,
                              .LocalUV = { 1.0f, 1.0f },
                              .Style = style,
                              .TextureIndex = quad.TextureIndex,
                              .Mode = mode });
    data.Vertices.push_back({ .Position = { quad.Rect.Min.x, quad.Rect.Max.y },
                              .UV = { quad.UVMin.x, quad.UVMax.y },
                              .Color = quad.ColorBottomLeft,
                              .LocalUV = { 0.0f, 1.0f },
                              .Style = style,
                              .TextureIndex = quad.TextureIndex,
                              .Mode = mode });

    data.Indices.push_back(base + 0);
    data.Indices.push_back(base + 1);
    data.Indices.push_back(base + 2);
    data.Indices.push_back(base + 0);
    data.Indices.push_back(base + 2);
    data.Indices.push_back(base + 3);
}

void AppendUIColorQuad(UIDrawData& data, 
                       const UIRect& rect, 
                       const glm::vec4 color,
                       const float radius,
                       const UIDrawMode mode,
                       const float borderWidth)
{
    KBR_TRACY_FUNCTION();
    
    UIQuad quad;
    quad.Rect = rect;
    quad.ColorTopLeft = color;
    quad.ColorTopRight = color;
    quad.ColorBottomLeft = color;
    quad.ColorBottomRight = color;
    quad.Radius = radius;
    quad.Mode = mode;
    quad.BorderWidth = borderWidth;
    AppendUIQuad(data, quad);
}

}
