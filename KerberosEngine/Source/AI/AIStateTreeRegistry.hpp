#pragma once

#include "AIStateTree.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Kerberos::AI {

enum class AIStateTreeOrigin : std::uint8_t
{
    BuiltIn,
    Script,
    Asset,
};

class AIStateTreeRegistry
{
public:
    static bool Register(std::string name, Ref<AIStateTree>, AIStateTreeOrigin = AIStateTreeOrigin::BuiltIn);
    static bool RegisterFromDocument(const std::string& name, std::string_view json, AIStateTreeOrigin, std::string* outError = nullptr);
    static bool Unregister(std::string_view name);

    static Ref<AIStateTree> Find(std::string_view name);
    static std::vector<std::string> GetNames();
    static void ClearScriptTrees();
    static void ClearAll();
};

}