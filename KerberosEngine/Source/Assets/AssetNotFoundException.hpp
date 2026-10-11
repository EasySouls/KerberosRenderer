#pragma once

#include "Core/EngineException.hpp"

#include <filesystem>

namespace Kerberos {

class AssetNotFoundException : public ResourceException
{
public:
    explicit AssetNotFoundException(std::filesystem::path assetPath,
                                    const std::source_location& location = std::source_location::current(),
                                    std::stacktrace trace = std::stacktrace::current())
        : ResourceException(std::format("Asset not found at path: '{}'", assetPath), location, std::move(trace)),
          m_AssetPath(std::move(assetPath))
    {
    }
    
    [[nodiscard]] const std::filesystem::path& AssetPath() const noexcept
    {
        return m_AssetPath;
    }

private:
    std::filesystem::path m_AssetPath;
};

}