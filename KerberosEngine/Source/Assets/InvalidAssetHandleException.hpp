#pragma once

#include "Asset.hpp"
#include "Core/EngineException.hpp"

namespace Kerberos {

class InvalidAssetHandleException : public ResourceException
{
public:
    explicit InvalidAssetHandleException(AssetHandle handle,
                                        const std::source_location& location = std::source_location::current(),
                                        std::stacktrace trace = std::stacktrace::current())
        : ResourceException(std::format("Invalid asset handle: '{}'", handle), location, std::move(trace)),
          m_Handle(handle)
    {
    }

    [[nodiscard]] AssetHandle Handle() const noexcept
    {
        return m_Handle;
    }

private:
    AssetHandle m_Handle;
};

}