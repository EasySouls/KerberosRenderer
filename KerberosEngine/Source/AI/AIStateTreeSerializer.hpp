#pragma once

#include "AIStateTreeDocument.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace Kerberos::AI {

/**
 * Parses and writes the authored AI state-tree document format. The parser
 * accepts YAML and JSON (JSON is a YAML subset), so existing C# documents remain
 * loadable. Unknown keys are ignored to keep the format forward compatible.
 */
class AIStateTreeSerializer
{
public:
    static bool Parse(std::string_view document, AIStateTreeDocument& out, std::string* outError = nullptr);
    static bool Serialize(const AIStateTreeDocument& document, std::string& outYaml, std::string* outError = nullptr);

    static bool LoadFile(const std::filesystem::path& path, AIStateTreeDocument& out, std::string* outError = nullptr);
    static bool SaveFile(const std::filesystem::path& path, const AIStateTreeDocument& document, std::string* outError = nullptr);
};

} // namespace Kerberos::AI
