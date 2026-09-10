#pragma once

#include "IAssetImporter.hpp"
#include "GLTFSceneImporter.hpp"
#include "Assets/Formats/NativeAssetFormat.hpp"
#include "Assets/Formats/NativeAssetSerializer.hpp"

#include <string_view>

#include "Profiling/Profilers.hpp"

namespace Kerberos {

		class GLTFPipelineImporter final : public IAssetImporter
		{
		public:
			bool SupportsExtension(std::string_view extension) const override;

			ImportResult Import(const ImportContext& context) override;

			ImporterType Type() const override;
		};
	}