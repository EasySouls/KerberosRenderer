#pragma once

#include "AINodeFactory.hpp"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Kerberos::AI {

/**
 * Authored transition data. Targets are stored by state name so the document stays
 * editable; the compiler hashes names into StateIDs.
 */
struct AIDocumentTransition
{
    std::string TargetState;
    std::string Name;
    std::vector<AINodeDesc> Conditions;
};

/**
 * Authored state data. Tasks and conditions keep their original descriptors
 * (type name plus parameters) so the editor can round-trip them without losing
 * parameter values.
 */
struct AIDocumentState
{
    std::string Name;
    std::vector<AINodeDesc> Tasks;
    std::vector<AIDocumentTransition> Transitions;
    /// Graph layout hint. Ignored by the runtime, serialized with the document.
    glm::vec2 EditorPosition = glm::vec2(0.0f);
};

/**
 * Source representation of an AI state tree. 
 * This is the editable, serializable form, AIStateTree is the compiled runtime form derived from it.
 */
struct AIStateTreeDocument
{
    std::string Name;
    std::string EntryState;
    std::vector<AIDocumentState> States;

    [[nodiscard]] AIDocumentState* FindState(std::string_view name);
    [[nodiscard]] const AIDocumentState* FindState(std::string_view name) const;
};

}
