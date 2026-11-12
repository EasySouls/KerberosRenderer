#pragma once

#include "AIEditorDocument.hpp"
#include "AI/AIStateTreeDocument.hpp"

#include <string>
#include <string_view>


namespace Kerberos {

AI::AIStateTreeDocument ProjectCompiledTree(const AI::AIStateTree& tree, std::string_view fallbackName = "");

std::string ResolveStateName(const AIEditorDocument& document, uint64_t stateEditorID);

uint64_t ResolveStateEditorID(const AIEditorDocument& document, AI::StateID stateID);

}