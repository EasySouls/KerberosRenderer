#include "AI/AINodeFactory.hpp"
#include "AI/AIStateTreeAsset.hpp"
#include "AI/AIStateTreeDocument.hpp"
#include "AI/AIStateTreeRegistry.hpp"
#include "AI/AIStateTreeSerializer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

using namespace Kerberos;
using namespace Kerberos::AI;

class AIStateTreeDocumentTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        AIStateTreeRegistry::ClearAll();
    }

    void TearDown() override
    {
        AIStateTreeRegistry::ClearAll();
    }
};

const AINodeParamDef* FindParamDef(const AINodeTypeInfo& info, const std::string_view name)
{
    for (const AINodeParamDef& param : info.Params)
    {
        if (param.Name == name)
            return &param;
    }
    return nullptr;
}

AIStateTreeDocument MakeChaseDocument()
{
    AIStateTreeDocument document;
    document.Name = "Enemy";
    document.EntryState = "Idle";

    AIDocumentState idle;
    idle.Name = "Idle";
    idle.EditorPosition = glm::vec2(120.0f, -40.0f);
    idle.Tasks.push_back(AINodeDesc{
        .Type = "Wait",
        .Params = { { "Duration", 2.5 } }
    });

    AIDocumentTransition transition;
    transition.TargetState = "Chase";
    transition.Name = "Spotted";
    transition.Conditions.push_back(AINodeDesc{ .Type = "TargetVisible" });
    transition.Conditions.push_back(AINodeDesc{
        .Type = "DistanceToTarget",
        .Params = {
            { "TargetKey", std::string("TargetEntity") },
            { "Distance", 12.0 },
            { "LessThan", true },
        }
    });
    idle.Transitions.push_back(std::move(transition));
    document.States.push_back(std::move(idle));

    AIDocumentState chase;
    chase.Name = "Chase";
    chase.Tasks.push_back(AINodeDesc{
        .Type = "ChaseTarget",
        .Params = {
            { "TargetKey", std::string("TargetEntity") },
            { "Speed", 4.5 },
        }
    });
    document.States.push_back(std::move(chase));

    return document;
}

} // namespace

TEST_F(AIStateTreeDocumentTest, SerializerRoundTripsDocument)
{
    const AIStateTreeDocument document = MakeChaseDocument();

    std::string yaml;
    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::Serialize(document, yaml, &error)) << error;
    EXPECT_FALSE(yaml.empty());

    AIStateTreeDocument parsed;
    ASSERT_TRUE(AIStateTreeSerializer::Parse(yaml, parsed, &error)) << error;

    EXPECT_EQ(parsed.Name, "Enemy");
    EXPECT_EQ(parsed.EntryState, "Idle");
    ASSERT_EQ(parsed.States.size(), 2u);

    const AIDocumentState& idle = parsed.States[0];
    EXPECT_EQ(idle.Name, "Idle");
    EXPECT_FLOAT_EQ(idle.EditorPosition.x, 120.0f);
    EXPECT_FLOAT_EQ(idle.EditorPosition.y, -40.0f);
    ASSERT_EQ(idle.Tasks.size(), 1u);
    EXPECT_EQ(idle.Tasks[0].Type, "Wait");

    const auto* duration = std::get_if<double>(&idle.Tasks[0].Params.at("Duration"));
    ASSERT_NE(duration, nullptr);
    EXPECT_DOUBLE_EQ(*duration, 2.5);

    ASSERT_EQ(idle.Transitions.size(), 1u);
    EXPECT_EQ(idle.Transitions[0].TargetState, "Chase");
    EXPECT_EQ(idle.Transitions[0].Name, "Spotted");
    ASSERT_EQ(idle.Transitions[0].Conditions.size(), 2u);
    EXPECT_EQ(idle.Transitions[0].Conditions[0].Type, "TargetVisible");

    const auto* lessThan = std::get_if<bool>(&idle.Transitions[0].Conditions[1].Params.at("LessThan"));
    ASSERT_NE(lessThan, nullptr);
    EXPECT_TRUE(*lessThan);

    const auto* distance = std::get_if<double>(&idle.Transitions[0].Conditions[1].Params.at("Distance"));
    ASSERT_NE(distance, nullptr);
    EXPECT_DOUBLE_EQ(*distance, 12.0);
}

TEST_F(AIStateTreeDocumentTest, SerializerRoundTripsVec3AndIntParameters)
{
    AIStateTreeDocument document;
    document.Name = "Params";
    document.EntryState = "Idle";

    AIDocumentState idle;
    idle.Name = "Idle";
    idle.Tasks.push_back(AINodeDesc{
        .Type = "Custom",
        .Params = {
            { "Offset", glm::vec3(1.5f, -2.0f, 3.25f) },
            { "Count", std::int64_t{ 7 } },
            { "Enabled", true },
            { "Label", std::string("hello") },
        }
    });
    document.States.push_back(std::move(idle));

    std::string yaml;
    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::Serialize(document, yaml, &error)) << error;

    AIStateTreeDocument parsed;
    ASSERT_TRUE(AIStateTreeSerializer::Parse(yaml, parsed, &error)) << error;

    const AINodeDesc& node = parsed.States[0].Tasks[0];
    const auto* offset = std::get_if<glm::vec3>(&node.Params.at("Offset"));
    ASSERT_NE(offset, nullptr);
    EXPECT_EQ(*offset, glm::vec3(1.5f, -2.0f, 3.25f));

    const auto* count = std::get_if<std::int64_t>(&node.Params.at("Count"));
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(*count, 7);

    const auto* enabled = std::get_if<bool>(&node.Params.at("Enabled"));
    ASSERT_NE(enabled, nullptr);
    EXPECT_TRUE(*enabled);

    const auto* label = std::get_if<std::string>(&node.Params.at("Label"));
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(*label, "hello");
}

TEST_F(AIStateTreeDocumentTest, RegistryStoresDocumentOriginAndCompiles)
{
    std::string yaml;
    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::Serialize(MakeChaseDocument(), yaml, &error)) << error;

    ASSERT_TRUE(AIStateTreeRegistry::RegisterFromDocument(
        "Enemy", yaml, AIStateTreeOrigin::Asset, &error)) << error;

    const Ref<AIStateTree> tree = AIStateTreeRegistry::Find("Enemy");
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->GetName(), "Enemy");
    EXPECT_EQ(tree->GetEntryState(), "Idle"_id);

    const AIStateDefinition* idle = tree->FindState("Idle"_id);
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->Tasks.size(), 1u);
    ASSERT_EQ(idle->Transitions.size(), 1u);
    EXPECT_EQ(idle->Transitions.front().TargetState, "Chase"_id);
    EXPECT_EQ(idle->Transitions.front().DebugName, "Spotted");
    ASSERT_EQ(idle->Transitions.front().Conditions.size(), 2u);

    const AIStateTreeDocument* stored = AIStateTreeRegistry::GetDocument("Enemy");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->Name, "Enemy");
    EXPECT_EQ(stored->EntryState, "Idle");
    ASSERT_EQ(stored->States.size(), 2u);

    const auto origin = AIStateTreeRegistry::GetOrigin("Enemy");
    ASSERT_TRUE(origin.has_value());
    EXPECT_TRUE(*origin == AIStateTreeOrigin::Asset);

    EXPECT_EQ(AIStateTreeRegistry::GetDocument("Missing"), nullptr);
    EXPECT_FALSE(AIStateTreeRegistry::GetOrigin("Missing").has_value());
}

TEST_F(AIStateTreeDocumentTest, RegisterFromDocumentUsesRegistryName)
{
    AIStateTreeDocument document;
    document.Name = "AuthoredName";
    document.EntryState = "Idle";
    document.States.push_back(AIDocumentState{ .Name = "Idle" });

    std::string yaml;
    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::Serialize(document, yaml, &error)) << error;
    ASSERT_TRUE(AIStateTreeRegistry::RegisterFromDocument(
        "RuntimeName", yaml, AIStateTreeOrigin::BuiltIn, &error)) << error;

    const AIStateTreeDocument* stored = AIStateTreeRegistry::GetDocument("RuntimeName");
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->Name, "RuntimeName");
    EXPECT_EQ(AIStateTreeRegistry::GetDocument("AuthoredName"), nullptr);
    EXPECT_NE(AIStateTreeRegistry::Find("RuntimeName"), nullptr);
}

TEST_F(AIStateTreeDocumentTest, ClearAssetTreesPreservesOtherOrigins)
{
    const auto makeTree = [](const char* name) {
        Ref<AIStateTree> tree = CreateRef<AIStateTree>(name);
        tree->AddState("Idle"_id, "Idle");
        tree->SetEntryState("Idle"_id);
        return tree;
    };

    ASSERT_TRUE(AIStateTreeRegistry::Register("BuiltInTree", makeTree("BuiltInTree")));
    ASSERT_TRUE(AIStateTreeRegistry::Register(
        "ScriptTree", makeTree("ScriptTree"), AIStateTreeOrigin::Script));

    AIStateTreeDocument document;
    document.Name = "AssetTree";
    document.EntryState = "Idle";
    document.States.push_back(AIDocumentState{ .Name = "Idle" });
    ASSERT_TRUE(AIStateTreeRegistry::RegisterDocument(
        "AssetTree", makeTree("AssetTree"), AIStateTreeOrigin::Asset, document));

    AIStateTreeRegistry::ClearAssetTrees();
    EXPECT_NE(AIStateTreeRegistry::Find("BuiltInTree"), nullptr);
    EXPECT_NE(AIStateTreeRegistry::Find("ScriptTree"), nullptr);
    EXPECT_EQ(AIStateTreeRegistry::Find("AssetTree"), nullptr);

    AIStateTreeRegistry::ClearScriptTrees();
    EXPECT_NE(AIStateTreeRegistry::Find("BuiltInTree"), nullptr);
    EXPECT_EQ(AIStateTreeRegistry::Find("ScriptTree"), nullptr);
}

TEST_F(AIStateTreeDocumentTest, StateTreeAssetLoadsCompilesAndRegisters)
{
    const std::filesystem::path source =
        std::filesystem::temp_directory_path() / "kerberos_ai_asset_load.kbrai";

    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::SaveFile(source, MakeChaseDocument(), &error)) << error;

    const Ref<AIStateTreeAsset> asset = AIStateTreeAsset::Load(AssetHandle(123), source, &error);
    ASSERT_NE(asset, nullptr) << error;
    EXPECT_EQ(asset->GetHandle(), AssetHandle(123));
    EXPECT_EQ(asset->Document.Name, "Enemy");
    ASSERT_NE(asset->Compiled, nullptr);
    EXPECT_EQ(asset->Compiled->GetEntryState(), "Idle"_id);

    const Ref<AIStateTree> registered = AIStateTreeRegistry::Find("Enemy");
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered.get(), asset->Compiled.get());

    const auto origin = AIStateTreeRegistry::GetOrigin("Enemy");
    ASSERT_TRUE(origin.has_value());
    EXPECT_TRUE(*origin == AIStateTreeOrigin::Asset);

    std::error_code ec;
    std::filesystem::remove(source, ec);
}

TEST_F(AIStateTreeDocumentTest, StateTreeAssetSavesEditedSource)
{
    const std::filesystem::path source =
        std::filesystem::temp_directory_path() / "kerberos_ai_asset_save_source.kbrai";
    const std::filesystem::path saved =
        std::filesystem::temp_directory_path() / "kerberos_ai_asset_save_target.kbrai";

    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::SaveFile(source, MakeChaseDocument(), &error)) << error;

    const Ref<AIStateTreeAsset> asset = AIStateTreeAsset::Load(AssetHandle(124), source, &error);
    ASSERT_NE(asset, nullptr) << error;

    asset->Document.EntryState = "Chase";
    ASSERT_TRUE(asset->Recompile(&error)) << error;
    EXPECT_EQ(asset->Compiled->GetEntryState(), "Chase"_id);
    ASSERT_TRUE(asset->SaveSource(saved, &error)) << error;

    AIStateTreeDocument reloaded;
    ASSERT_TRUE(AIStateTreeSerializer::LoadFile(saved, reloaded, &error)) << error;
    EXPECT_EQ(reloaded.EntryState, "Chase");
    ASSERT_NE(reloaded.FindState("Chase"), nullptr);

    std::error_code ec;
    std::filesystem::remove(source, ec);
    std::filesystem::remove(saved, ec);
}

TEST_F(AIStateTreeDocumentTest, CompileDocumentRejectsInvalidDocuments)
{
    std::string error;

    AIStateTreeDocument document;
    document.Name = "Invalid";
    document.EntryState = "Idle";
    document.States.push_back(AIDocumentState{ .Name = "Idle" });

    EXPECT_NE(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_TRUE(error.empty());

    error.clear();
    document.EntryState = "Missing";
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    document.EntryState = "Idle";
    document.States[0].Transitions.push_back(AIDocumentTransition{ .TargetState = "Missing" });
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    document.States[0].Transitions.clear();
    document.States[0].Tasks.push_back(AINodeDesc{ .Type = "NotARealTask" });
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    document.States[0].Tasks.clear();
    document.States.push_back(AIDocumentState{ .Name = "Idle" });
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    document.States.pop_back();
    document.States[0].Name.clear();
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    document.States[0].Name = "Idle";
    document.Name.clear();
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());

    error.clear();
    document.Name = "Invalid";
    document.States.clear();
    EXPECT_EQ(AIStateTreeRegistry::CompileDocument(document, &error), nullptr);
    EXPECT_FALSE(error.empty());
}

TEST_F(AIStateTreeDocumentTest, StandardNodeSchemaIsAvailable)
{
    const std::vector<AINodeTypeInfo> tasks = GetAINodeTypes(AINodeKind::Task);
    const auto chase = std::find_if(tasks.begin(), tasks.end(), [](const AINodeTypeInfo& info) {
        return info.Type == "ChaseTarget";
    });
    ASSERT_NE(chase, tasks.end());
    EXPECT_TRUE(chase->Kind == AINodeKind::Task);

    const AINodeParamDef* speed = FindParamDef(*chase, "Speed");
    ASSERT_NE(speed, nullptr);
    EXPECT_TRUE(speed->Type == AINodeParamType::Float);
    const auto* speedDefault = std::get_if<double>(&speed->Default);
    ASSERT_NE(speedDefault, nullptr);
    EXPECT_DOUBLE_EQ(*speedDefault, 4.0);

    const AINodeParamDef* targetKey = FindParamDef(*chase, "TargetKey");
    ASSERT_NE(targetKey, nullptr);
    EXPECT_TRUE(targetKey->Type == AINodeParamType::String);

    const std::vector<AINodeTypeInfo> conditions = GetAINodeTypes(AINodeKind::Condition);
    const auto visible = std::find_if(conditions.begin(), conditions.end(), [](const AINodeTypeInfo& info) {
        return info.Type == "TargetVisible";
    });
    ASSERT_NE(visible, conditions.end());
    EXPECT_TRUE(visible->Params.empty());

    const auto found = FindAINodeType("DistanceToTarget", AINodeKind::Condition);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->Params.size(), 3u);
    EXPECT_FALSE(FindAINodeType("TargetVisible", AINodeKind::Task).has_value());
}

TEST_F(AIStateTreeDocumentTest, CompiledNodesExposeDebugType)
{
    std::string yaml;
    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::Serialize(MakeChaseDocument(), yaml, &error)) << error;
    ASSERT_TRUE(AIStateTreeRegistry::RegisterFromDocument(
        "Enemy", yaml, AIStateTreeOrigin::Script, &error)) << error;

    const Ref<AIStateTree> tree = AIStateTreeRegistry::Find("Enemy");
    ASSERT_NE(tree, nullptr);

    const AIStateDefinition* idle = tree->FindState("Idle"_id);
    ASSERT_NE(idle, nullptr);
    ASSERT_EQ(idle->Tasks.size(), 1u);
    EXPECT_EQ(idle->Tasks.front()->GetDebugType(), "Wait");

    ASSERT_EQ(idle->Transitions.size(), 1u);
    ASSERT_EQ(idle->Transitions.front().Conditions.size(), 2u);
    EXPECT_EQ(idle->Transitions.front().Conditions[0]->GetDebugType(), "TargetVisible");
    EXPECT_EQ(idle->Transitions.front().Conditions[1]->GetDebugType(), "DistanceToTarget");
}

TEST_F(AIStateTreeDocumentTest, ParserIgnoresUnknownFieldsAndAcceptsNullTransitionName)
{
    constexpr std::string_view document = R"yaml(
Name: NullName
Entry: A
UnknownRoot: 1
States:
  - Name: A
    UnknownState: true
    Transitions:
      - To: B
        Name: null
        When: []
  - Name: B
)yaml";

    AIStateTreeDocument parsed;
    std::string error;
    ASSERT_TRUE(AIStateTreeSerializer::Parse(document, parsed, &error)) << error;

    EXPECT_EQ(parsed.Name, "NullName");
    EXPECT_EQ(parsed.EntryState, "A");
    ASSERT_EQ(parsed.States.size(), 2u);
    ASSERT_EQ(parsed.States[0].Transitions.size(), 1u);
    EXPECT_EQ(parsed.States[0].Transitions[0].TargetState, "B");
    EXPECT_TRUE(parsed.States[0].Transitions[0].Name.empty());
}

TEST_F(AIStateTreeDocumentTest, ParserRejectsMalformedDocuments)
{
    std::string error;
    AIStateTreeDocument document;

    EXPECT_FALSE(AIStateTreeSerializer::Parse(R"yaml(States: [])yaml", document, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(AIStateTreeSerializer::Parse(R"yaml(
Entry: A
States:
  - Name: A
  - Name: A
)yaml", document, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(AIStateTreeSerializer::Parse(R"yaml(
Entry: A
States:
  - Name: A
    Tasks:
      - Type: Custom
        Params:
          Offset: [1.0, 2.0]
)yaml", document, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(AIStateTreeSerializer::Parse(R"yaml(
Entry: A
States:
  - Name: A
    Tasks:
      - Params: {}
)yaml", document, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(AIStateTreeSerializer::Parse(R"yaml(
Entry: A
States:
  - Name: A
    Transitions:
      - When: []
)yaml", document, &error));
    EXPECT_FALSE(error.empty());
}
