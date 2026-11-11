#include "AIEditorDocumentCommand.hpp"

#include <utility>

namespace Kerberos {

AIEditorDocumentCommand::AIEditorDocumentCommand(AIEditorDocument* document,
                                                 AIEditorDocumentSnapshot before,
                                                 AIEditorDocumentSnapshot after,
                                                 std::string name)
    : m_Target(document)
    , m_Before(std::move(before))
    , m_After(std::move(after))
    , m_Name(std::move(name))
{
}

void AIEditorDocumentCommand::Execute()
{
    Apply(m_After);
}

void AIEditorDocumentCommand::Undo()
{
    Apply(m_Before);
}

const std::string& AIEditorDocumentCommand::GetName() const
{
    return m_Name;
}

void AIEditorDocumentCommand::Apply(const AIEditorDocumentSnapshot& snapshot) const
{
    if (!m_Target)
        return;

    m_Target->Document = snapshot.Document;
    m_Target->States = snapshot.States;
    m_Target->Transitions = snapshot.Transitions;
    m_Target->NextEditorID = snapshot.NextEditorID;
    m_Target->Dirty = true;
}

} // namespace Kerberos
