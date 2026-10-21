#pragma once

#include "AssetMetadata.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace Kerberos::AssetUri
{
	/// URI scheme prefixes. The generic form is `<scheme>://<path>`, but for
	/// brevity the engine uses `<scheme>::/<path>` (e.g. `project::/Textures/x.png`,
	/// `engine::/Editor/play_button.png`), mirroring Godot's `res://`/`user://`.
	inline constexpr std::string_view ProjectScheme = "project::/";
	inline constexpr std::string_view EngineScheme = "engine::/";

	/// Builds a URI string from a source root and a root-relative path.
	inline std::string Build(const AssetSourceRoot root, const std::filesystem::path& relativePath)
	{
		const std::string_view scheme = root == AssetSourceRoot::Engine ? EngineScheme : ProjectScheme;
		std::string uri(scheme);
		uri += relativePath.generic_string();
		return uri;
	}

	/// Splits a URI into its source root and relative path. Returns false when
	/// the URI has an unknown or missing scheme.
	inline bool Parse(const std::string_view uri, AssetSourceRoot& outRoot, std::filesystem::path& outPath)
	{
		if (uri.starts_with(EngineScheme))
		{
			outRoot = AssetSourceRoot::Engine;
			outPath = uri.substr(EngineScheme.size());
			return true;
		}
		if (uri.starts_with(ProjectScheme))
		{
			outRoot = AssetSourceRoot::Project;
			outPath = uri.substr(ProjectScheme.size());
			return true;
		}
		return false;
	}
}
