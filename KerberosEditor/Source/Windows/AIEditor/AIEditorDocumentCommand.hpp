#pragma once

#include "AIEditorDocument.hpp"
#include "AI/AIStateTreeDocument.hpp"
#include "Undo/ICommand.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

struct AIEditorDocumentSnapshot
{
    AI::AIStateTreeDocument Document;
    std::vector<AIEditorStateRef> States;
    std::vector<AIEditorTransitionRef> Transitions;
    uint64_t NextEditorID = 1;
};

class AIEditorDocumentCommand final : public ICommand
{
public:
    AIEditorDocumentCommand(AIEditorDocument* document,
                            AIEditorDocumentSnapshot before,
                            AIEditorDocumentSnapshot after,
                            std::string name);

    void Execute() override;
    void Undo() override;

    [[nodiscard]] const std::string& GetName() const override;

private:
    void Apply(const AIEditorDocumentSnapshot& snapshot) const;

    AIEditorDocument* m_Target = nullptr;
    AIEditorDocumentSnapshot m_Before;
    AIEditorDocumentSnapshot m_After;
    std::string m_Name;
};

} // namespace Kerberos
