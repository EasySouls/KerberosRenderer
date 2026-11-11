#pragma once

#include "AIEditorDocument.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos {

enum class AIEditorIssueSeverity : std::uint8_t
{
    Error,
    Warning,
    Info
};

struct AIEditorIssue
{
    AIEditorIssueSeverity Severity = AIEditorIssueSeverity::Error;
    std::string Message;
    std::string StateName;
    int32_t TransitionIndex = -1;
    uint64_t StateEditorID = 0;       // 0 when not applicable
    uint64_t TransitionEditorID = 0;  // 0 when not applicable
};

struct AIEditorValidationResult
{
    std::vector<AIEditorIssue> Issues;

    [[nodiscard]] bool HasErrors() const;
    [[nodiscard]] bool IsValid() const { return !HasErrors(); }
};

/// Pure editor-side validation. It uses the engine compiler for runtime parity,
/// then adds structural issues that can be selected in the graph.
class AIEditorValidator
{
public:
    [[nodiscard]] static AIEditorValidationResult Validate(const AIEditorDocument& document);
};

} // namespace Kerberos
