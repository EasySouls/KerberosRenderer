#pragma once

#include <filesystem>

namespace Kerberos
{
	/// Resolves locations of engine-owned content that ships with the engine
	/// rather than living in the user's project. The engine root is the
	/// directory that contains `Assets/`, `Resources/`, and (for the editor)
	/// `Library/`.
	///
	/// This exists so engine code never relies on the process working directory
	/// to find editor-provided assets, shaders, fonts, or the managed script
	/// assembly. The working directory may differ from the engine install
	/// location, especially when the user's project lives elsewhere.
	class EnginePaths
	{
	public:
		static void Initialize(const std::filesystem::path& engineRoot)
		{
			s_Root = engineRoot.empty() ? std::filesystem::current_path() : engineRoot;
		}

		static const std::filesystem::path& GetRoot()
		{
			return s_Root;
		}

		static std::filesystem::path Assets()
		{
			return s_Root / "Assets";
		}

		static std::filesystem::path Shaders()
		{
			return Assets() / "Shaders";
		}

		static std::filesystem::path ShaderCache()
		{
#ifdef KBR_DEBUG
			return Assets() / "Cache" / "Shaders" / "Debug";
#else
			return Assets() / "Cache" / "Shaders";
#endif
		}

		static std::filesystem::path Resources()
		{
			return s_Root / "Resources";
		}

		static std::filesystem::path ScriptAssemblies()
		{
			return Resources() / "Scripts";
		}

	private:
		inline static std::filesystem::path s_Root{ std::filesystem::current_path() };
	};
}
