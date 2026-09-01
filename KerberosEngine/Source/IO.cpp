#include "IO.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

import Kerberos;

namespace Kerberos::IO {

std::vector<char> ReadFile(const std::filesystem::path& filepath)
{
    std::ifstream file(filepath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        Log::CoreError("Failed to open file: {}", filepath.string());
        throw std::runtime_error("failed to open file!");
    }

    std::vector<char> buffer(static_cast<size_t>(file.tellg()));

    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));

    file.close();

    return buffer;
}

}