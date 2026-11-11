#include "Windows/AIEditor/AIEditorConversions.hpp"
#include "Windows/AIEditor/AIEditorDocument.hpp"
#include "Windows/AIEditor/AIEditorDocumentCommand.hpp"
#include "Windows/AIEditor/AIEditorGraphIds.hpp"
#include "Windows/AIEditor/AIEditorNodeSchema.hpp"
#include "Windows/AIEditor/AIEditorValidator.hpp"

#include "AI/AINodes.hpp"
#include "AI/AIStateTreeRegistry.hpp"
#include "AI/AIStateTreeSerializer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

import Kerberos;

namespace {

using namespace Kerberos;
using namespace Kerberos::AI;

AIEditorDocument MakeDocument()
{
    AIEditorDocument document;
    document.Document.Name = "TestTree";
    document.Document.EntryState = "Idle";
    return document;
}

} // namespace

TEST(AIEditorModelTest, GraphReferencesTrackDocumentOrder)
{
    AIEditorDocument document = MakeDocument();
    const uint64_t idleID = document.AddState("Idle", glm::vec2(0.0f, 0.0f));
    const uint64_t chaseID = document.AddState("Chase", glm::vec2(300.0f, 0.0f));
    const uint64_t transitionID = document.AddTransition(0, "Chase", "Spotted");

    ASSERT_NE(idleID, 0u);
    ASSERT_NE(chaseID, 0u);
    ASSERT_NE(transitionID, 0u);
    ASSERT_EQ(document.States.size(), 2u);
    ASSERT_EQ(document.Transitions.size(), 1u);
    EXPECT_EQ(document.FindState(idleID)->DocumentStateIndex, 0u);
    EXPECT_EQ(document.FindState(chaseID)->DocumentStateIndex, 1u);

    document.RenameState(chaseID, "Run");
    EXPECT_EQ(document.Document.States[1].Name, "Run");
    EXPECT_EQ(document.Document.States[0].Transitions[0].TargetState, "Run");
    EXPECT_EQ(document.ResolveState(*document.FindState(chaseID))->Name, "Run");

    document.RemoveState(chaseID);
    ASSERT_EQ(document.States.size(), 1u);
    EXPECT_EQ(document.Transitions.size(), 0u);
    EXPECT_TRUE(document.Document.States[0].Transitions.empty());
    EXPECT_EQ(document.Document.EntryState, "Idle");
}

TEST(AIEditorModelTest, ValidatorReportsErrorsAndWarnings)
{
    AIEditorDocument empty = MakeDocument();
    empty.Document.EntryState.clear();
    EXPECT_TRUE(AIEditorValidator::Validate(empty).HasErrors());

    AIEditorDocument warningOnly = MakeDocument();
    warningOnly.AddState("Idle");
    const AIEditorValidationResult warningResult = AIEditorValidator::Validate(warningOnly);
    EXPECT_FALSE(warningResult.HasErrors());
    EXPECT_FALSE(warningResult.Issues.empty());

    AIEditorDocument badTarget = MakeDocument();
    badTarget.AddState("Idle");
    badTarget.AddTransition(0, "Missing", "Bad");
    EXPECT_TRUE(AIEditorValidator::Validate(badTarget).HasErrors());

    AIEditorDocument unknownTask = MakeDocument();
    const uint64_t stateID = unknownTask.AddState("Idle");
    AI::AIDocumentState* state = unknownTask.ResolveState(*unknownTask.FindState(stateID));
    ASSERT_NE(state, nullptr);
    state->Tasks.push_back(AI::AINodeDesc{ .Type = "DoesNotExist" });
    EXPECT_TRUE(AIEditorValidator::Validate(unknownTask).HasErrors());

    AIEditorDocument duplicate = MakeDocument();
    duplicate.AddState("Idle");
    duplicate.AddState("Idle");
    EXPECT_TRUE(AIEditorValidator::Validate(duplicate).HasErrors());
}

TEST(AIEditorModelTest, CommandAppliesAndRestoresSnapshots)
{
    AIEditorDocument document = MakeDocument();
    document.AddState("Idle");

    const AIEditorDocumentSnapshot before = CaptureSnapshot(document);
    document.AddState("Chase");
    document.AddTransition(0, "Chase", "Go");
    const AIEditorDocumentSnapshot after = CaptureSnapshot(document);

    AIEditorDocumentCommand command(&document, before, after, "Add Chase");
    command.Undo();
    EXPECT_EQ(document.Document.States.size(), 1u);
    EXPECT_TRUE(document.Document.States[0].Transitions.empty());

    command.Execute();
    EXPECT_EQ(document.Document.States.size(), 2u);
    EXPECT_EQ(document.Document.States[0].Transitions.size(), 1u);
    EXPECT_EQ(document.Document.States[0].Transitions[0].TargetState, "Chase");
}

TEST(AIEditorModelTest, CompiledTreeProjectsToReadOnlyDocument)
{
    Ref<AIStateTree> tree = CreateRef<AIStateTree>("CompiledTree");
    AIStateDefinition& idle = tree->AddState("Idle"_id, "Idle");
    AIStateDefinition& chase = tree->AddState("Chase"_id, "Chase");

    Ref<TaskStop> stop = CreateRef<TaskStop>();
    stop->SetDebugType("Stop");
    idle.Tasks.push_back(stop);

    Ref<ConditionTargetVisible> visible = CreateRef<ConditionTargetVisible>();
    visible->SetDebugType("TargetVisible");
    idle.Transitions.push_back(AITransition{
        .TargetState = "Chase"_id,
        .Conditions = { visible },
        .DebugName = "Spotted"
    });
    tree->SetEntryState("Idle"_id);

    const AIStateTreeDocument projected = ProjectCompiledTree(*tree, "Fallback");
    EXPECT_EQ(projected.Name, "CompiledTree");
    EXPECT_EQ(projected.EntryState, "Idle");
    ASSERT_EQ(projected.States.size(), 2u);

    const AI::AIDocumentState* idleState = projected.FindState("Idle");
    ASSERT_NE(idleState, nullptr);
    ASSERT_EQ(idleState->Tasks.size(), 1u);
    EXPECT_EQ(idleState->Tasks[0].Type, "Stop");
    ASSERT_EQ(idleState->Transitions.size(), 1u);
    EXPECT_EQ(idleState->Transitions[0].TargetState, "Chase");
    EXPECT_EQ(idleState->Transitions[0].Name, "Spotted");
    ASSERT_EQ(idleState->Transitions[0].Conditions.size(), 1u);
    EXPECT_EQ(idleState->Transitions[0].Conditions[0].Type, "TargetVisible");

    AIEditorDocument editorDocument;
    editorDocument.Document = projected;
    editorDocument.RebuildGraphReferences();
    const uint64_t idleEditorID = ResolveStateEditorID(editorDocument, "Idle"_id);
    ASSERT_NE(idleEditorID, 0u);
    EXPECT_EQ(ResolveStateName(editorDocument, idleEditorID), "Idle");
}

TEST(AIEditorModelTest, SampleEnemyChaseAssetIsValid)
{
    const std::filesystem::path path =
        std::filesystem::path(KBR_SOURCE_DIR) / "KerberosEditor/Assets/AIStateTrees/EnemyChase.kbrai";

    AI::AIStateTreeDocument document;
    std::string error;
    ASSERT_TRUE(AI::AIStateTreeSerializer::LoadFile(path, document, &error)) << error;

    AIEditorDocument editorDocument;
    editorDocument.Document = document;
    editorDocument.RebuildGraphReferences();

    const AIEditorValidationResult validation = AIEditorValidator::Validate(editorDocument);
    std::ostringstream issues;
    for (const AIEditorIssue& issue : validation.Issues)
        issues << issue.Message << '\n';

    EXPECT_FALSE(validation.HasErrors()) << issues.str();
    EXPECT_EQ(editorDocument.States.size(), 3u);
    EXPECT_EQ(editorDocument.Transitions.size(), 6u);
}

TEST(AIEditorModelTest, GraphIDsAreUniqueAcrossKinds)
{
    using namespace AIEditorGraphId;

    EXPECT_NE(MakeStateNodeId(7), MakeStateInputPinId(7));
    EXPECT_NE(MakeStateNodeId(7), MakeTransitionOutputPinId(7));
    EXPECT_NE(MakeStateInputPinId(7), MakeTransitionLinkId(7));
    EXPECT_EQ(MakeStateNodeId(7), MakeStateNodeId(7));
}

TEST(AIEditorModelTest, NextGraphReferencesResetForReload)
{
    AIEditorDocument document = MakeDocument();
    document.AddState("Idle");
    document.AddState("Chase");
    document.AddTransition(0, "Chase", "Go");

    document.RebuildGraphReferences();
    EXPECT_EQ(document.States.size(), 2u);
    EXPECT_EQ(document.Transitions.size(), 1u);
    EXPECT_EQ(document.States[0].EditorID, 1u);
    EXPECT_EQ(document.States[1].EditorID, 2u);
    EXPECT_EQ(document.Transitions[0].EditorID, 3u);
}

TEST(AIEditorModelTest, RemoveStatesByDocumentIndexIsBatchSafe)
{
    AIEditorDocument document = MakeDocument();
    const uint64_t idleID = document.AddState("Idle");
    const uint64_t chaseID = document.AddState("Chase");
    document.AddState("Attack");
    document.AddTransition(0, "Chase", "Spotted");
    document.AddTransition(1, "Attack", "InRange");

    ASSERT_EQ(document.States.size(), 3u);
    const size_t idleIndex = document.FindState(idleID)->DocumentStateIndex;
    const size_t chaseIndex = document.FindState(chaseID)->DocumentStateIndex;

    // Deliberately unsorted: the batch must resolve every index against the
    // pre-removal document instead of the rebuilt session IDs.
    const size_t indices[] = { chaseIndex, idleIndex };
    document.RemoveStates(indices);

    ASSERT_EQ(document.Document.States.size(), 1u);
    EXPECT_EQ(document.Document.States[0].Name, "Attack");
    EXPECT_TRUE(document.Document.States[0].Transitions.empty());
    EXPECT_TRUE(document.Transitions.empty());
}

TEST(AIEditorModelTest, RemoveStatesRepairsEntryState)
{
    AIEditorDocument document = MakeDocument();
    const uint64_t idleID = document.AddState("Idle");
    const uint64_t chaseID = document.AddState("Chase");
    document.AddState("Attack");

    document.RemoveState(chaseID);
    EXPECT_EQ(document.Document.EntryState, "Idle");

    document.RemoveState(idleID);
    EXPECT_EQ(document.Document.EntryState, "Attack");

    const size_t remainingCount = document.Document.States.size();
    std::vector<size_t> allIndices;
    for (size_t i = 0; i < remainingCount; ++i)
        allIndices.push_back(i);

    document.RemoveStates(allIndices);
    EXPECT_TRUE(document.Document.States.empty());
    EXPECT_TRUE(document.Document.EntryState.empty());
}

TEST(AIEditorModelTest, RemoveTransitionsByIndexIsBatchSafe)
{
    AIEditorDocument document = MakeDocument();
    document.AddState("Idle");
    document.AddState("Chase");
    document.AddState("Attack");
    document.AddTransition(0, "Chase", "A");
    document.AddTransition(0, "Attack", "B");
    document.AddTransition(1, "Idle", "C");

    const std::pair<size_t, size_t> items[] = { { 0, 1 }, { 0, 0 } };
    document.RemoveTransitions(items);

    ASSERT_EQ(document.Document.States.size(), 3u);
    EXPECT_TRUE(document.Document.States[0].Transitions.empty());
    ASSERT_EQ(document.Document.States[1].Transitions.size(), 1u);
    EXPECT_EQ(document.Document.States[1].Transitions[0].Name, "C");
    EXPECT_EQ(document.Transitions.size(), 1u);
}

TEST(AIEditorModelTest, MoveTransitionByIndexReordersAndReturnsEditorID)
{
    AIEditorDocument document = MakeDocument();
    document.AddState("Idle");
    document.AddState("Chase");
    document.AddTransition(0, "Chase", "A");
    document.AddTransition(0, "Chase", "B");
    document.AddTransition(0, "Chase", "C");

    ASSERT_EQ(document.Document.States[0].Transitions.size(), 3u);

    const uint64_t movedID = document.MoveTransitionByIndex(0, 0, 2);
    ASSERT_NE(movedID, 0u);

    const AIEditorTransitionRef* moved = document.FindTransition(movedID);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->SourceStateIndex, 0u);
    EXPECT_EQ(moved->TransitionIndex, 2u);

    const std::vector<AI::AIDocumentTransition>& transitions =
        document.Document.States[0].Transitions;
    ASSERT_EQ(transitions.size(), 3u);
    EXPECT_EQ(transitions[0].Name, "B");
    EXPECT_EQ(transitions[1].Name, "C");
    EXPECT_EQ(transitions[2].Name, "A");
}

TEST(AIEditorModelTest, NodeSchemaBuildsTypedDefaults)
{
    const std::optional<AI::AINodeTypeInfo> info =
        AI::FindAINodeType("Wait", AI::AINodeKind::Task);
    ASSERT_TRUE(info.has_value());

    const AI::AINodeDesc desc = MakeDefaultNodeDesc(*info);
    EXPECT_EQ(desc.Type, "Wait");
    ASSERT_EQ(desc.Params.size(), info->Params.size());

    const auto durationIt = desc.Params.find("Duration");
    ASSERT_NE(durationIt, desc.Params.end());
    const double* duration = std::get_if<double>(&durationIt->second);
    ASSERT_NE(duration, nullptr);
    EXPECT_DOUBLE_EQ(*duration, 1.0);
}

TEST(AIEditorModelTest, BlackboardKeyParametersAreDetected)
{
    EXPECT_TRUE(IsBlackboardKeyParam("Key"));
    EXPECT_TRUE(IsBlackboardKeyParam("TargetKey"));
    EXPECT_TRUE(IsBlackboardKeyParam("SomeOtherKey"));
    EXPECT_FALSE(IsBlackboardKeyParam("Speed"));
}

TEST(AIEditorModelTest, UniqueNamesAvoidExistingEntries)
{
    AIEditorDocument document = MakeDocument();
    document.AddState("Idle");
    document.AddState("New State");
    document.AddTransition(0, "New State", "Transition");

    EXPECT_EQ(document.GenerateUniqueStateName(), "New State 2");
    EXPECT_EQ(document.GenerateUniqueStateName("Chase"), "Chase");

    const AIEditorStateRef* idleRef = document.FindState(document.States.front().EditorID);
    ASSERT_NE(idleRef, nullptr);
    const AI::AIDocumentState* idle = document.ResolveState(*idleRef);
    ASSERT_NE(idle, nullptr);
    EXPECT_EQ(document.GenerateUniqueTransitionName(*idle), "Transition 2");
}
