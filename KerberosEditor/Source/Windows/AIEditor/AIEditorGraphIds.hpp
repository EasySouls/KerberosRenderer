#pragma once

#include <cstdint>

namespace Kerberos::AIEditorGraph {

/// Session editor IDs are packed with a small kind tag so node, pin, and link
/// ID spaces cannot collide in imgui-node-editor.
enum class GraphIdKind : std::uintptr_t
{
    StateNode = 1,
    StateInputPin = 2,
    TransitionOutputPin = 3,
    NewTransitionPin = 4,
    TransitionLink = 5
};

constexpr std::uintptr_t Make(const std::uint64_t editorID, const GraphIdKind kind) noexcept
{
    return (static_cast<std::uintptr_t>(editorID) << 3) | static_cast<std::uintptr_t>(kind);
}

constexpr std::uintptr_t MakeStateNodeId(const std::uint64_t editorID) noexcept
{
    return Make(editorID, GraphIdKind::StateNode);
}

constexpr std::uintptr_t MakeStateInputPinId(const std::uint64_t editorID) noexcept
{
    return Make(editorID, GraphIdKind::StateInputPin);
}

constexpr std::uintptr_t MakeTransitionOutputPinId(const std::uint64_t transitionEditorID) noexcept
{
    return Make(transitionEditorID, GraphIdKind::TransitionOutputPin);
}

constexpr std::uintptr_t MakeNewTransitionPinId(const std::uint64_t stateEditorID) noexcept
{
    return Make(stateEditorID, GraphIdKind::NewTransitionPin);
}

constexpr std::uintptr_t MakeTransitionLinkId(const std::uint64_t transitionEditorID) noexcept
{
    return Make(transitionEditorID, GraphIdKind::TransitionLink);
}

} // namespace Kerberos::AIEditorGraphIds
