#pragma once

#include <filesystem>
#include <vector>

namespace Kerberos::IO {

std::vector<char> ReadFile(const std::filesystem::path& filepath);

}