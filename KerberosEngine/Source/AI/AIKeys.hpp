#pragma once 

#include "AIIdentifier.hpp"

namespace Kerberos::AI {

inline constexpr BlackboardKey TargetEntity = "TargetEntity"_bb;
inline constexpr BlackboardKey HasLineOfSight = "HasLineOfSight"_bb;
inline constexpr BlackboardKey LastKnownPosition = "LastKnownPosition"_bb;
inline constexpr BlackboardKey StateTime = "StateTime"_bb;

}