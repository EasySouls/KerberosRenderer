#include "kbrpch.hpp"
#include "UIFontLibrary.hpp"

#include "Core/EnginePaths.hpp"
#include "Project/Project.hpp"
#include "Renderer/TextureManager.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <system_error>

import Kerberos;

namespace Kerberos {

namespace {

constexpr std::array<double, 7> FontEmBuckets = { 16.0, 20.0, 24.0, 32.0, 40.0, 48.0, 64.0 };

std::string NormalizePathString(std::filesystem::path path)
{
    KBR_TRACY_FUNCTION();
    
	path.make_preferred();
	return path.generic_string();
}

bool IsFontFile(const std::filesystem::path& path)
{
	const std::string extension = path.extension().string();
	return extension == ".ttf" || extension == ".otf";
}

void CollectFontFiles(const std::filesystem::path& directory,
                      const std::filesystem::path& authoredRoot,
                      std::vector<std::string>& out)
{
    KBR_TRACY_FUNCTION();
    
	std::error_code error;
	if (!std::filesystem::exists(directory, error))
		return;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(
	         directory, std::filesystem::directory_options::skip_permission_denied, error))
	{
		if (error)
			break;
		if (!entry.is_regular_file(error) || !IsFontFile(entry.path()))
			continue;

		std::error_code relativeError;
		const auto relative = std::filesystem::relative(entry.path(), authoredRoot, relativeError);
		if (relativeError)
			continue;

		out.push_back(NormalizePathString(relative));
	}
}

}

UIFontLibrary& UIFontLibrary::Get()
{
	static UIFontLibrary instance;
	return instance;
}

void UIFontLibrary::Initialize(TextureManager* textureManager)
{
	m_TextureManager = textureManager;
}

void UIFontLibrary::Shutdown()
{
	m_Entries.clear();
	m_ResolvedPaths.clear();
	m_TextureManager = nullptr;
}

std::string UIFontLibrary::DefaultFontPath()
{
	return "Fonts/Inter/Inter_18pt-Regular.ttf";
}

std::filesystem::path UIFontLibrary::ResolveFontPath(const std::string& authoredPath)
{
    KBR_TRACY_FUNCTION();
    
	if (authoredPath.empty())
		return ResolveFontPath(DefaultFontPath());

	std::filesystem::path path(authoredPath);
	std::error_code error;

	if (path.is_absolute())
		return std::filesystem::exists(path, error) ? path : std::filesystem::path{};

	if (const Ref<Project> project = Project::GetActive())
	{
		const std::filesystem::path projectPath = Project::GetAssetDirectory() / path;
		if (std::filesystem::exists(projectPath, error))
			return projectPath;
	}

	const std::filesystem::path enginePath = EnginePaths::Assets() / path;
	if (std::filesystem::exists(enginePath, error))
		return enginePath;

	return {};
}

std::string UIFontLibrary::MakeAuthoredPath(const std::filesystem::path& absolutePath)
{
    KBR_TRACY_FUNCTION();
    
	std::error_code error;

	if (const Ref<Project> project = Project::GetActive())
	{
		const auto relative = std::filesystem::relative(absolutePath, Project::GetAssetDirectory(), error);
		if (!error && !relative.empty() && relative.generic_string().rfind("..", 0) != 0)
			return NormalizePathString(relative);
	}

	const auto engineRelative = std::filesystem::relative(absolutePath, EnginePaths::Assets(), error);
	if (!error && !engineRelative.empty() && engineRelative.generic_string().rfind("..", 0) != 0)
		return NormalizePathString(engineRelative);

	return NormalizePathString(absolutePath);
}

std::vector<std::string> UIFontLibrary::DiscoverFontFiles()
{
    KBR_TRACY_FUNCTION();
    
	std::vector<std::string> files;

	if (const Ref<Project> project = Project::GetActive())
	{
		const auto projectAssetDirectory = Project::GetAssetDirectory();
		CollectFontFiles(projectAssetDirectory / "Fonts", projectAssetDirectory, files);
		if (files.empty())
			CollectFontFiles(projectAssetDirectory, projectAssetDirectory, files);
	}

	const auto engineAssets = EnginePaths::Assets();
	CollectFontFiles(engineAssets / "Fonts", engineAssets, files);

	std::ranges::sort(files);
	files.erase(std::ranges::unique(files).begin(), files.end());
	return files;
}

double UIFontLibrary::SelectEmSize(const float fontSize)
{
    KBR_TRACY_FUNCTION();
    
	const double requested = std::clamp<double>(fontSize, 8.0, 96.0);
	for (const double bucket : FontEmBuckets)
	{
		if (bucket >= requested)
			return bucket;
	}
	return FontEmBuckets.back();
}

Ref<Font> UIFontLibrary::Get(const std::string& authoredPath, const float fontSize)
{
    KBR_TRACY_FUNCTION();
    
	std::filesystem::path resolved;
	if (const auto cached = m_ResolvedPaths.find(authoredPath); cached != m_ResolvedPaths.end())
	{
		resolved = cached->second;
	}
	else
	{
		resolved = ResolveFontPath(authoredPath);
		m_ResolvedPaths[authoredPath] = resolved;
	}

	if (resolved.empty())
	{
		Log::CoreWarn("UI font '{}' could not be resolved; falling back to the default UI font.",
		              authoredPath.empty() ? "<empty>" : authoredPath);
		resolved = ResolveFontPath(DefaultFontPath());
		if (resolved.empty())
		{
			KBRAssert(false, "Default UI font could not be resolved: {}", DefaultFontPath());
			return nullptr;
		}
		m_ResolvedPaths[authoredPath] = resolved;
	}

	const double emSize = SelectEmSize(fontSize);
	const std::string key = NormalizePathString(resolved) + "#" + std::to_string(static_cast<int>(emSize));

	auto& entry = m_Entries[key];
	if (entry.Font)
		return entry.Font;

	entry.Font = CreateRef<Font>(resolved.stem().string(), resolved, emSize);

	if (m_TextureManager && entry.Font->GetAtlasTexture())
		entry.TextureIndex = m_TextureManager->RegisterTexture(entry.Font->GetAtlasTexture());

	return entry.Font;
}

uint32_t UIFontLibrary::GetTextureIndex(const Ref<Font>& font)
{
    KBR_TRACY_FUNCTION();
    
	if (!font)
		return 0;

	for (const auto& [Font, TextureIndex] : m_Entries | std::views::values)
	{
		if (Font == font)
			return TextureIndex;
	}

	if (m_TextureManager && font->GetAtlasTexture())
		return m_TextureManager->RegisterTexture(font->GetAtlasTexture());

	return 0;
}

}
