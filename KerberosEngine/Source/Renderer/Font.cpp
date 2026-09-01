#include "Font.hpp"
#include "Core/Core.hpp"

#include "Core/Timer.hpp"

#undef INFINITE
#include "Profiling/Instrumentor.hpp"

#include <msdfgen.h>
#include "ext/import-font.h"
#include <msdf-atlas-gen/msdf-atlas-gen.h>
#include <msdf-atlas-gen/msdf-atlas-gen/FontGeometry.h>
#include <msdf-atlas-gen/msdf-atlas-gen/GlyphGeometry.h>
#include "msdf-atlas-gen/AtlasGenerator.h"

#include <algorithm>
#include <thread>

import Kerberos;

namespace Kerberos
{
	struct MSDFData
	{
		std::vector<msdf_atlas::GlyphGeometry> Glyphs;
		msdf_atlas::FontGeometry FontGeometry;
	};

	template<typename T, typename S, int N, msdf_atlas::GeneratorFunction<S, N> GenFunc>
	static Ref<Texture2D> GenerateAtlas(const std::vector<msdf_atlas::GlyphGeometry>& glyphs, int width, int height)
	{
		msdf_atlas::ImmediateAtlasGenerator<S, N, GenFunc, msdf_atlas::BitmapAtlasStorage<T, N>> generator(width, height);

		msdf_atlas::GeneratorAttributes genAttributes;
		genAttributes.config.overlapSupport = true;
		genAttributes.scanlinePass = true;
		generator.setAttributes(genAttributes);
		generator.setThreadCount(std::max(1u, std::thread::hardware_concurrency() - 1));

		generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

		msdfgen::BitmapConstRef<T, N> bitmap = generator.atlasStorage();

		TextureSpecification spec;
		spec.Width = bitmap.width;
		spec.Height = bitmap.height;
		spec.Format = ImageFormat::RGBA8;
		spec.GenerateMips = false;

		Buffer textureBuffer;
		const uint64_t bufferSize = bitmap.width * bitmap.height * sizeof(T) * N;
		textureBuffer.Allocate(bufferSize);
		std::memcpy(textureBuffer.Data, bitmap.pixels, bufferSize);

		const Ref<Texture2D> atlasTexture = Texture2D::FromBuffer(spec, textureBuffer);
		//atlasTexture->SetData(static_cast<void*>(const_cast<T*>(bitmap.pixels)), bitmap.width * bitmap.height * sizeof(T) * N);

		return atlasTexture;
	}

	Font::Font(std::string name, const std::filesystem::path& filepath)
		: m_Name(std::move(name)), m_Filepath(filepath), m_MSDFData(new MSDFData{})
	{
		KBR_PROFILE_FUNCTION();

		Timer timer("Font::Font", [&](const TimerData& data) {
			Log::CoreInfo("Timer: Loading font {0} from {1} took {2}ms", m_Name, filepath.string(), data.DurationMs);
		});

		if (!std::filesystem::exists(filepath))
		{
			KBRAssert(false, "Font file does not exist: {0}", filepath.string());
			return;
		}

		msdfgen::FreetypeHandle* ft = msdfgen::initializeFreetype();
		if (!ft)
		{
			KBRAssert(false, "Could not initialize FreeType library!");
			return;
		}

		const std::string filepathStr = filepath.string();
		msdfgen::FontHandle* font = msdfgen::loadFont(ft, filepathStr.c_str());
		if (!font)
		{
			KBRAssert(false, "Could not load font: {0}", filepath.string());
			msdfgen::deinitializeFreetype(ft);
			return;
		}

		struct CharsetRange
		{
			uint32_t Start;
			uint32_t End;
		};

		static constexpr uint32_t ranges[] = { 0x0020, 0x00FF, 0 }; // Basic Latin

		static constexpr CharsetRange charsetRanges[] = {
			{.Start = 0x0020, .End = 0x007E }, // Basic Latin
			{.Start = 0x00A0, .End = 0x00FF }, // Latin-1 Supplement
		};

		msdf_atlas::Charset charset;
		for (const auto& [Start, End] : charsetRanges)
		{
			for (uint32_t c = Start; c <= End; ++c)
			{
				charset.add(c);
			}
		}

		constexpr double fontScale = 1.0f;

		m_MSDFData->FontGeometry = msdf_atlas::FontGeometry(&m_MSDFData->Glyphs);
		int glyphsLoaded = m_MSDFData->FontGeometry.loadCharset(font, fontScale, charset);

		msdf_atlas::TightAtlasPacker atlasPacker;
		atlasPacker.setPixelRange(2.0);
		atlasPacker.setMiterLimit(1.0);
		atlasPacker.setSpacing(1);
		atlasPacker.setDimensionsConstraint(msdf_atlas::DimensionsConstraint::POWER_OF_TWO_RECTANGLE);
		atlasPacker.setOriginPixelAlignment(true);
		double emSize = 40.0;
		atlasPacker.setScale(emSize);

		int remaining = atlasPacker.pack(m_MSDFData->Glyphs.data(), static_cast<int>(m_MSDFData->Glyphs.size()));
		KBRAssert(remaining == 0, "Could not pack all glyphs into the atlas! {} glyphs remaining", remaining);

		int width, height;
		atlasPacker.getDimensions(width, height);
		emSize = atlasPacker.getScale();

		constexpr uint64_t lcgMultiplier = 6364136223846793005ull;
		constexpr uint64_t lcgIncrement = 1442695040888963407ull;

		constexpr bool expensiveEdgeColoring = true;
		constexpr uint64_t coloringSeed = 0x12345678abcdef00ull;
		if (expensiveEdgeColoring)
		{
			const int threadCount = static_cast<int>(std::max(1u, std::thread::hardware_concurrency() - 1));
			msdf_atlas::Workload([&glyphs = m_MSDFData->Glyphs](const int i, int) -> bool {
				const uint64_t glyphSeed = (lcgMultiplier * (coloringSeed ^ i) + lcgIncrement) * !!coloringSeed;
				glyphs[i].edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, glyphSeed);
				return true;
			},
								 static_cast<int>(m_MSDFData->Glyphs.size())
			).finish(threadCount);
		}
		else
		{
			uint64_t glyphSeed = coloringSeed;
			for (msdf_atlas::GlyphGeometry& glyph : m_MSDFData->Glyphs)
			{
				glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, 3.0, glyphSeed);
				glyphSeed = glyphSeed * lcgMultiplier + lcgIncrement;
			}
		}

		m_AtlasTexture = GenerateAtlas<uint8_t, float, 4, msdf_atlas::mtsdfGenerator>(m_MSDFData->Glyphs, width, height);

		msdfgen::destroyFont(font);
		msdfgen::deinitializeFreetype(ft);
	}

	Font::~Font()
	{
		delete m_MSDFData;
	}

	FontMetrics Font::GetMetrics() const
	{
		const auto& msdfMetrics = m_MSDFData->FontGeometry.getMetrics();

		FontMetrics metrics;
		metrics.Ascender = static_cast<float>(msdfMetrics.ascenderY);
		metrics.Descender = static_cast<float>(msdfMetrics.descenderY);
		metrics.LineHeight = static_cast<float>(msdfMetrics.lineHeight);
		return metrics;
	}

	bool Font::HasCharacter(const char c) const
	{
		return m_MSDFData->FontGeometry.getGlyph(c) != nullptr;
	}

	void Font::GetQuadAtlasBounds(char character, double& al, double& ab, double& ar, double& at) const
	{
		const auto& glyph = m_MSDFData->FontGeometry.getGlyph(character);
		KBRAssert(glyph, "Font does not contain character: {0}", character);

		glyph->getQuadAtlasBounds(al, ab, ar, at);

	}

	void Font::GetQuadPlaneBounds(char character, double& pl, double& pb, double& pr, double& pt) const
	{
		const auto& glyph = m_MSDFData->FontGeometry.getGlyph(character);
		KBRAssert(glyph, "Font does not contain character: {0}", character);

		glyph->getQuadPlaneBounds(pl, pb, pr, pt);
	}

	double Font::GetAdvance(char character) const
	{
		const auto& glyph = m_MSDFData->FontGeometry.getGlyph(character);
		KBRAssert(glyph, "Font does not contain character: {0}", character);

		return glyph->getAdvance();
	}

	void Font::GetNextAdvance(double& advance, const char character, const char nextCharacter) const
	{
		m_MSDFData->FontGeometry.getAdvance(advance, character, nextCharacter);
	}
}