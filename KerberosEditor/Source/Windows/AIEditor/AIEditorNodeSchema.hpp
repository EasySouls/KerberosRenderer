#pragma once

#include "AI/AINodeFactory.hpp"

#include <array>
#include <string_view>

namespace Kerberos {

AI::AINodeDesc MakeDefaultNodeDesc(const AI::AINodeTypeInfo& info);

bool IsBlackboardKeyParam(std::string_view name);

inline constexpr std::array<std::string_view, 4> standardBlackboardKeyNames{
    "TargetEntity",
    "HasLineOfSight",
    "LastKnownPosition",
    "StateTime"
};

}
