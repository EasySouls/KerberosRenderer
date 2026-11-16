#pragma once 

#include "Core/Core.hpp"
#include "Textures/Texture2D.hpp"

#include <filesystem>
#include <string>

namespace Kerberos
{
	struct MSDFData;

	struct FontMetrics
	{
		float Ascender = 0.0f;
		float Descender = 0.0f;
		float LineHeight = 0.0f;
	};

	class Font
	{
	public:
		explicit Font(std::string name, const std::filesystem::path& filepath, double emSize = 40.0);
		~Font();

		Font(const Font& other) = delete;
		Font(Font&& other) noexcept = default;
		Font& operator=(const Font& other) = delete;
		Font& operator=(Font&& other) noexcept = default;

		Ref<Texture2D> GetAtlasTexture() const { return m_AtlasTexture; }
		FontMetrics GetMetrics() const;
		const std::string& GetName() const { return m_Name; }
		const std::filesystem::path& GetFilepath() const { return m_Filepath; }
		/// Atlas em size this font was rasterized at. Used to pick a crisp size bucket.
		double GetEmSize() const { return m_EmSize; }

		bool HasCharacter(char c) const;
		void GetQuadAtlasBounds(char character, double& al, double& ab, double& ar, double& at) const;
		void GetQuadPlaneBounds(char character, double& pl, double& pb, double& pr, double& pt) const;
		double GetAdvance(char character) const;
		void GetNextAdvance(double& advance, char character, char nextCharacter) const;

	private:
		std::string m_Name;
		std::filesystem::path m_Filepath;
		double m_EmSize = 40.0;

		MSDFData* m_MSDFData = nullptr;
		Ref<Texture2D> m_AtlasTexture;
	};
}