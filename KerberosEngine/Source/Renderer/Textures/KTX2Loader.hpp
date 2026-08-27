#pragma once

#include <filesystem>

struct ktxTexture2;

namespace Kerberos::KTX2Loader {

[[nodiscard]] bool IsKTX2(const std::filesystem::path& filepath);
[[nodiscard]] bool Load(const std::filesystem::path& filepath, ktxTexture2** texture);

}
