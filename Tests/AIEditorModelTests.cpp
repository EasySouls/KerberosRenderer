#include "Windows/AIEditor/AIEditorConversions.hpp"
#include "Windows/AIEditor/AIEditorDocument.hpp"
#include "Windows/AIEditor/AIEditorDocumentCommand.hpp"
#include "Windows/AIEditor/AIEditorGraphIds.hpp"
#include "Windows/AIEditor/AIEditorValidator.hpp"

#include "AI/AINodes.hpp"
#include "AI/AIStateTreeRegistry.hpp"
#include "AI/AIStateTreeSerializer.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

import Kerberos;

namespace {

using namespace Kerberos;
using namespace Kerberos::AI;

AIEditorDocumentSnapshot CaptureSnapshot(const AIEditorDocument& document)
{
    return AIEditorDocumentSnapshot{
        .Document = document.Document,
        .States = document.States,
        .Transitions = document.Transitions,
        .NextEditorID = document.NextEditorID
    };
}

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
    using namespace AIEditorGraphIds;

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
