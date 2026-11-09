#include "AIStateTreeDocument.hpp"

namespace Kerberos::AI {

AIDocumentState* AIStateTreeDocument::FindState(const std::string_view name)
{
    for (AIDocumentState& state : States)
    {
        if (state.Name == name)
            return &state;
    }

    return nullptr;
}

const AIDocumentState* AIStateTreeDocument::FindState(const std::string_view name) const
{
    for (const AIDocumentState& state : States)
    {
        if (state.Name == name)
            return &state;
    }

    return nullptr;
}

} // namespace Kerberos::AI
