#include "HierarchyPanel.hpp"

#include "AssetConstants.hpp"
#include "Assets/AssetManager.hpp"
#include "Assets/Importers/TextureImporter.hpp"
#include "Events/KeyPressedEvent.hpp"
#include "Input/InputSystem.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/AudioComponents.hpp"
#include "Scene/Components/ParticleComponents.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Renderer/Animation/Skeleton.hpp"
#include "Renderer/Animation/SkeletalMesh.hpp"
#include "Scripting/ScriptClass.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptInstance.hpp"
#include "Profiling/Profilers.hpp"
#include "VulkanContext.hpp"
#include "Undo/CreateEntityCommand.hpp"
#include "Undo/DestroyEntityCommand.hpp"
#include "Undo/DuplicateEntityCommand.hpp"
#include "Undo/InstantiatePrefabCommand.hpp"
#include "Undo/SetComponentValueCommand.hpp"
#include "Undo/UndoRedoManager.hpp"
#include "Widgets/AssetPicker.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <algorithm>
#include <filesystem>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <utility>


import Kerberos;

namespace Kerberos {

namespace
{
    void CreateProceduralSkinnedTest(const Ref<Scene>& scene)
    {
        if (!scene)
            return;

        std::vector<SkeletonJoint> joints;
        joints.push_back({
            .Name = "Root",
            .Parent = -1,
            .SourceNode = 0,
            .RestTranslation = { 0.0f, 0.0f, 0.0f },
            .RestRotation = { 1.0f, 0.0f, 0.0f, 0.0f },
            .RestScale = { 1.0f, 1.0f, 1.0f },
            .InverseBind = glm::mat4(1.0f),
        });
        joints.push_back({
            .Name = "Tip",
            .Parent = 0,
            .SourceNode = 1,
            .RestTranslation = { 0.0f, 2.0f, 0.0f },
            .RestRotation = { 1.0f, 0.0f, 0.0f, 0.0f },
            .RestScale = { 1.0f, 1.0f, 1.0f },
            .InverseBind = glm::mat4(1.0f),
        });

        auto skeleton = CreateRef<Skeleton>(joints);
        skeleton->GetHandle() = UUID();

        constexpr int Rows = 5;
        constexpr int Cols = 2;

        std::vector<SkinVertex> vertices;
        vertices.reserve(Rows * Cols);
        for (int row = 0; row < Rows; ++row)
        {
            const float v = static_cast<float>(row) / static_cast<float>(Rows - 1);
            for (int col = 0; col < Cols; ++col)
            {
                const float u = static_cast<float>(col) / static_cast<float>(Cols - 1);
                SkinVertex vertex{};
                vertex.Position = { -0.75f + 1.5f * u, -0.5f + 2.0f * v, 0.0f };
                vertex.Normal = { 0.0f, 0.0f, 1.0f };
                vertex.Tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                vertex.TexCoord = { u, v };
                vertex.JointIndices = { 0, 1, 0, 0 };
                vertex.JointWeights = { 1.0f - v, v, 0.0f, 0.0f };
                vertices.push_back(vertex);
            }
        }

        std::vector<uint32_t> indices;
        for (int row = 0; row + 1 < Rows; ++row)
        {
            for (int col = 0; col + 1 < Cols; ++col)
            {
                const uint32_t i0 = static_cast<uint32_t>(row * Cols + col);
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + Cols;
                const uint32_t i3 = i2 + 1;

                indices.insert(indices.end(), { i0, i1, i3, i0, i3, i2 });
                // Reverse winding so the test mesh is visible from both sides.
                indices.insert(indices.end(), { i0, i3, i1, i0, i2, i3 });
            }
        }

        auto mesh = CreateRef<SkeletalMesh>("Procedural Skinned Strip", vertices, indices);
        mesh->GetHandle() = UUID();

        Entity skeletonEntity = scene->CreateEntity("Skinned Test Skeleton");
        auto& skeletonComponent = skeletonEntity.AddComponent<SkeletonComponent>();
        skeletonComponent.SkeletonAsset = skeleton->GetHandle();
        skeletonComponent.Skeleton = skeleton;

        Entity meshEntity = scene->CreateEntity("Skinned Test Mesh");
        auto& meshComponent = meshEntity.AddComponent<SkinnedMeshComponent>();
        meshComponent.MeshAsset = mesh->GetHandle();
        meshComponent.SkeletonAsset = skeleton->GetHandle();
        meshComponent.SkeletonEntity = skeletonEntity.GetUUID();
        meshComponent.Mesh = mesh;
        meshComponent.Visible = true;

        scene->SetParent(meshEntity, skeletonEntity, false);

        Log::EditorInfo("Created procedural skinned test mesh (bind pose)");
    }

    std::string GetAssetStemLabel(const AssetHandle handle)
    {
        if (!handle.IsValid() || !AssetManager::IsAssetHandleValid(handle))
            return "Missing asset";

        const auto& metadata = Project::GetActive()->GetEditorAssetManager()->GetMetadata(handle);
        std::string label = metadata.Filepath.stem().string();
        if (label.empty())
            label = metadata.SubAssetKey;
        if (label.empty())
            label = std::format("Asset {}", static_cast<uint64_t>(handle));
        return label;
    }

    std::optional<AssetHandle> DrawScriptAssetReferenceField(
        const char* label,
        const uint64_t currentHandle,
        const AssetType type,
        const char* hint = nullptr)
    {
        AssetHandle handle = AssetHandle(currentHandle);
        if (AssetPicker::DrawAssetHandle(label, type, handle, hint))
            return handle;
        return std::nullopt;
    }

    std::optional<AssetHandle> DrawScriptAnyAssetReferenceField(
        const char* label,
        const uint64_t currentHandle)
    {
        AssetHandle handle = AssetHandle(currentHandle);
        if (AssetPicker::DrawAnyAssetHandle(label, handle))
            return handle;
        return std::nullopt;
    }

    std::string GetEntityDisplayName(const Scene* scene, const uint64_t entityID)
    {
        if (entityID == 0)
            return "None";

        const Entity entity = scene != nullptr
            ? scene->TryGetEntityByUUID(UUID(entityID))
            : Entity{};
        if (!entity)
            return "Missing Entity";

        return std::string(entity.GetName());
    }

    std::optional<uint64_t> DrawScriptEntityReferenceField(
        const char* label,
        const Scene* scene,
        const uint64_t currentEntityID)
    {
        std::optional<uint64_t> newEntityID;

        ImGui::PushID(label);

        const std::string display = std::string(label) + ": " +
            GetEntityDisplayName(scene, currentEntityID);

        const std::string buttonLabel = display + "##EntityRef";
        if (ImGui::Button(buttonLabel.c_str(), ImVec2(-70.0f, 0.0f)))
        {
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(hierarchyEntity))
            {
                if (payload->DataSize == sizeof(uint64_t))
                    newEntityID = *static_cast<const uint64_t*>(payload->Data);
                else
                    Log::EditorError("Invalid entity drag payload size: {0}", payload->DataSize);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear", ImVec2(60.0f, 0.0f)))
            newEntityID = 0;

        ImGui::PopID();

        return newEntityID;
    }

    template <typename TComponent>
    std::optional<UUID> DrawComponentEntityReferenceField(
        const char* label,
        const Ref<Scene>& scene,
        const UUID currentEntity)
    {
        std::optional<UUID> newEntity = std::nullopt;
        if (!scene)
            return newEntity;

        std::vector<Entity> candidates;
        for (const entt::entity root : scene->GetRootEntities())
            candidates.emplace_back(root, scene.get());
        for (size_t index = 0; index < candidates.size(); ++index)
        {
            for (const Entity child : scene->GetChildren(candidates[index]))
                candidates.push_back(child);
        }

        candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const Entity entity)
        {
            return !entity.HasComponent<TComponent>();
        }), candidates.end());

        std::string currentLabel = "None";
        if (currentEntity.IsValid())
        {
            const Entity entity = scene->TryGetEntityByUUID(currentEntity);
            currentLabel = entity && entity.HasComponent<TComponent>()
                ? std::string(entity.GetName()) : "Missing Entity";
        }

        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::BeginCombo("##ComponentEntityPicker", currentLabel.c_str()))
        {
            if (ImGui::Selectable("None", !currentEntity.IsValid()))
            {
                newEntity = UUID::Invalid();
                ImGui::CloseCurrentPopup();
            }

            for (const Entity entity : candidates)
            {
                ImGui::PushID(static_cast<int>(static_cast<uint64_t>(entity.GetUUID()) & 0x7fffffff));
                const std::string entityName(entity.GetName());
                if (ImGui::Selectable(entityName.c_str(), currentEntity == entity.GetUUID()))
                {
                    newEntity = entity.GetUUID();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(hierarchyEntity))
            {
                if (payload->DataSize == sizeof(uint64_t))
                {
                    const UUID dropped(*static_cast<const uint64_t*>(payload->Data));
                    const Entity droppedEntity = scene->TryGetEntityByUUID(dropped);
                    if (droppedEntity && droppedEntity.HasComponent<TComponent>())
                        newEntity = dropped;
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
        return newEntity;
    }
}


HierarchyPanel::HierarchyPanel(const Ref<Scene>& context) : m_Context(context)
{
    SetContext(context);
}

void HierarchyPanel::SetContext(const Ref<Scene>& context)
{
    m_Context = context;

    /// This is neccessary to avoid having a dangling pointer to the previous context
    /// when loading a new scene
    m_SelectedEntity = {};
    m_PendingSelectedEntity = {};
    m_Vec3EditSnapshots.clear();
    m_TagBeforeEdit.clear();
    m_TagEditEntity = UUID::Invalid();
    m_TagEditActive = false;

    /*m_CubeMesh = Mesh::CreateCube(1.0f);
    m_SphereMesh = Mesh::CreateSphere(1.0f, 16, 16);*/
    m_CubeMesh = AssetManager::GetDefaultCubeMesh();
    m_SphereMesh = nullptr;
    m_WhiteMaterial = CreateRef<Material>();

    m_WhiteTexture = AssetManager::GetDefaultTexture2D();

    /*FramebufferSpecification frameBufferSpec;
    frameBufferSpec.Width = 256;
    frameBufferSpec.Height = 256;
    frameBufferSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
    m_CubemapFramebuffer = Framebuffer::Create(frameBufferSpec);
    m_CubemapFramebuffer->SetDebugName("Depth Map Visualization");*/
}

void HierarchyPanel::OnImGuiRender()
{
    KBR_TRACY_FUNCTION();

    ImGui::Begin("Hierarchy");

    const bool dragDropTargetActive = HandleHierarchyPanelDragAndDrop();

    constexpr ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;

    const char* sceneName = m_Context ? m_Context->GetName().c_str() : "No Scene Loaded";
    if (ImGui::TreeNodeEx(reinterpret_cast<const void*>(sceneName), flags, "%s", sceneName)) {
        /// Only draw the root nodes
        for (const entt::entity& entityId : m_Context->GetRootEntities()) {
            Entity rootEntity{ entityId, m_Context.get() };
            DrawEntityNode(rootEntity);
        }

        ImGui::TreePop();
    }

    /// Apply a deferred entity selection once the mouse button is released.
    /// While a drag-and-drop operation is active the click that started it is
    /// treated as a drag instead, so the inspector keeps showing the original
    /// selection (the entity whose script field receives the drop).
    if (m_PendingSelectedEntity) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !ImGui::IsDragDropActive()) {
                SetSelectedEntity(m_PendingSelectedEntity);
            }
            m_PendingSelectedEntity = {};
        }
    }

    /// If an empty space is clicked, deselect the entity
    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
        m_SelectedEntity = {};
        if (m_OnEntitySelected) {
            m_OnEntitySelected(m_SelectedEntity);
        }
    }

    /// If the right mouse button is clicked, show the context menu
    if (ImGui::BeginPopupContextWindow(nullptr, 1 | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            ExecuteCreateEntity("Empty Entity");
        }

        if (ImGui::MenuItem("Create Skinned Test Mesh")) {
            CreateProceduralSkinnedTest(m_Context);
        }

        ImGui::EndPopup();
    }

    if (dragDropTargetActive)
        ImGui::EndDragDropTarget();

    ImGui::End();

    for (const auto& e : m_DeletionQueue) {
        const std::string tag = e.GetComponent<TagComponent>().Tag;
        m_NotificationManager.AddNotification("Entity deleted: " + tag, Notification::Type::Info);

        if (m_UndoRedo)
            m_UndoRedo->Execute(std::make_unique<DestroyEntityCommand>(m_Context, e));
        else
            m_Context->DestroyEntity(e);

        if (m_OnEntityDeleted) {
            m_OnEntityDeleted(e);
        }
    }
    m_DeletionQueue.clear();

    m_NotificationManager.RenderNotifications();
}

void HierarchyPanel::SetUndoRedoManager(UndoRedoManager* manager)
{
    m_UndoRedo = manager;
}

void HierarchyPanel::SetSelectedEntity(const Entity entity)
{
    m_SelectedEntity = entity;
    if (m_OnEntitySelected) {
        m_OnEntitySelected(entity);
    }
}

void HierarchyPanel::OnEvent(Event& event)
{
    EventDispatcher dispatcher(event);
    dispatcher.Dispatch<KeyPressedEvent>(KBR_BIND_FN(HierarchyPanel::OnKeyPressed));
}

void HierarchyPanel::SetOnEntityDeleted(const std::function<void(Entity)>& callback)
{
    m_OnEntityDeleted = callback;
}

void HierarchyPanel::SetOnEntitySelected(const std::function<void(Entity)>& callback)
{
    m_OnEntitySelected = callback;
}

void HierarchyPanel::DrawEntityNode(const Entity& entity)
{
    const auto& tag = entity.GetComponent<TagComponent>().Tag;

    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                     ImGuiTreeNodeFlags_DefaultOpen |
                                     (entity == m_SelectedEntity ? ImGuiTreeNodeFlags_Selected : 0);

    /// The entity's identifier serves as the unique ID for the ImGui tree node
    const bool opened =
        ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uint64_t>(entity.GetUUID())), flags, "%s", tag.c_str());

    if (ImGui::IsItemClicked()) {
        /// Defer the selection to mouse release so that starting a drag from
        /// this node does not switch the inspector away from the entity whose
        /// script field is the drop target.
        m_PendingSelectedEntity = entity;
    }

    if (ImGui::BeginDragDropSource()) {
        const uint64_t entityID = static_cast<uint64_t>(entity.GetUUID());
        ImGui::SetDragDropPayload(hierarchyEntity, &entityID, sizeof(entityID));
        ImGui::TextUnformatted(tag.c_str());
        ImGui::EndDragDropSource();
    }

    bool entityDeleted = false;
    DrawEntityNodeContextMenu(entity, entityDeleted);

    if (opened) {
        DrawEntityNodeChildren(entity);
        ImGui::TreePop();
    }

    if (entityDeleted) {
        QueueEntityDeletion(entity);
    }
}

void HierarchyPanel::DrawEntityNodeContextMenu(const Entity& entity, bool& entityDeleted) const
{
    if (!ImGui::BeginPopupContextItem())
        return;

    if (ImGui::MenuItem("Delete Entity", "Delete")) 
    {
        /// Defer the deletion of the entity until the popup is closed to avoid invalidating the iterator
        entityDeleted = true;
    }
    if (ImGui::MenuItem("Duplicate Entity", "Ctrl + D")) 
    {
        if (m_UndoRedo)
        {
            m_UndoRedo->Execute(std::make_unique<DuplicateEntityCommand>(m_Context, entity, true));
        }
        else
        {
            m_Context->DuplicateEntity(entity, true);
        }
    }
    if (ImGui::MenuItem("Create child")) 
    {
        if (m_UndoRedo)
        {
            m_UndoRedo->Execute(std::make_unique<CreateEntityCommand>(m_Context, "Unnamed", entity.GetUUID()));
        }
        else
        {
            m_Context->CreateChild(entity);
        }
    }

    ImGui::EndPopup();
}

void HierarchyPanel::DrawEntityNodeChildren(const Entity& entity)
{
    for (const Entity& child : m_Context->GetChildren(entity)) {
        DrawEntityNode(child);
    }
}

void HierarchyPanel::QueueEntityDeletion(const Entity& entity)
{
    m_DeletionQueue.push_back(entity);
    if (m_SelectedEntity == entity) {
        m_SelectedEntity = {};
    }
}

void HierarchyPanel::ExecuteCreateEntity(const std::string& name)
{
    if (m_UndoRedo)
        m_UndoRedo->Execute(std::make_unique<CreateEntityCommand>(m_Context, name));
    else
        m_Context->CreateEntity(name);
}

void HierarchyPanel::AddComponentPopup(const Entity entity)
{
    if (ImGui::Button("Add Component")) {
        ImGui::OpenPopup("Add Component");
    }

    if (ImGui::BeginPopup("Add Component")) {
        if (ImGui::MenuItem("Camera")) {
            AddComponentWithCheck<CameraComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Script")) {
            AddComponentWithCheck<ScriptComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Sprite Renderer")) {
            AddComponentWithCheck<SpriteRendererComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Directional Light")) {
            AddComponentWithCheck<DirectionalLightComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Point Light")) {
            AddComponentWithCheck<PointLightComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Static Mesh")) {
            AddComponentWithCheck<StaticMeshComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::BeginMenu("Animation")) {
            if (ImGui::MenuItem("Skeleton")) {
                AddComponentWithCheck<SkeletonComponent>(entity);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Skinned Mesh")) {
                AddComponentWithCheck<SkinnedMeshComponent>(entity);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Animator")) {
                AddComponentWithCheck<AnimatorComponent>(entity);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::MenuItem("Joint")) {
                AddComponentWithCheck<JointComponent>(entity);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Physics")) {

            if (ImGui::MenuItem("Rigidbody3D")) {
                AddComponentWithCheck<RigidBody3DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Box Collider 3D")) {
                AddComponentWithCheck<BoxCollider3DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Sphere Collider 3D")) {
                AddComponentWithCheck<SphereCollider3DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Capsule Collider 3D")) {
                AddComponentWithCheck<CapsuleCollider3DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Mesh Collider 3D")) {
                AddComponentWithCheck<MeshCollider3DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Environment")) {
            AddComponentWithCheck<EnvironmentComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::MenuItem("Text")) {
            AddComponentWithCheck<TextComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        if (ImGui::BeginMenu("Audio")) {
            if (ImGui::MenuItem("Audio Source 2D")) {
                AddComponentWithCheck<AudioSource2DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Audio Source 3D")) {
                AddComponentWithCheck<AudioSource3DComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            if (ImGui::MenuItem("Audio Listener")) {
                AddComponentWithCheck<AudioListenerComponent>(entity);
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Particle Emitter")) {
            AddComponentWithCheck<ParticleEmitterComponent>(entity);
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool HierarchyPanel::OnKeyPressed(const KeyPressedEvent& event)
{
    /// Shortcuts
    if (event.GetRepeatCount() > 0)
        return false;

    const bool ctrl = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
    [[maybe_unused]]
    const bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);

    switch (event.GetKeyCode()) {
    case Key::Delete:
        if (m_SelectedEntity) {
            m_DeletionQueue.push_back(m_SelectedEntity);
            m_SelectedEntity = {};
            return true;
        }
        break;
    case Key::D:
        if (m_SelectedEntity && ctrl) {
            if (m_UndoRedo)
                m_UndoRedo->Execute(std::make_unique<DuplicateEntityCommand>(m_Context, m_SelectedEntity, true));
            else
                m_Context->DuplicateEntity(m_SelectedEntity, true);
            return true;
        }
        break;
    default:
        break;
    }

    return false;
}

bool HierarchyPanel::HandleHierarchyPanelDragAndDrop()
{
    KBR_TRACY_FUNCTION();

    if (!ImGui::BeginDragDropTarget())
        return false;

    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserMesh)) {
        const AssetHandle handle = ReadAssetDragPayload(payload->Data, payload->DataSize).Handle;
        const AssetType assetType = AssetManager::GetAssetType(handle);

        if (assetType == AssetType::Prefab) {
            if (m_UndoRedo) {
                auto command = std::make_unique<InstantiatePrefabCommand>(m_Context, handle);
                command->Execute();

                if (command->GetEntityID().IsValid()) {
                    if (const Entity prefabEntity = m_Context->TryGetEntityByUUID(command->GetEntityID()))
                        m_SelectedEntity = prefabEntity;
                    m_UndoRedo->Record(std::move(command));
                }
            }
            else if (const Entity prefabEntity = m_Context->InstantiatePrefab(handle)) {
                m_SelectedEntity = prefabEntity;
            }
        }
        else if (assetType == AssetType::Mesh) {
            Entity meshEntity = m_Context->CreateEntity("Mesh");
            auto& smc = meshEntity.AddComponent<StaticMeshComponent>();
            smc.StaticMesh = AssetManager::AsAsset<Mesh>(handle);
            m_SelectedEntity = meshEntity;
        }
    }

    return true;
}

void HierarchyPanel::DrawVec3Control(const std::string& label,
                                     const UUID entityID,
                                     glm::vec3& values,
                                     const float resetValue,
                                     const float columnWidth,
                                     const std::function<void()>& onValueChanged,
                                     const std::function<void(const glm::vec3&)>& onEditCommitted)
{
    const ImGuiIO& io = ImGui::GetIO();
    const auto boldFont = io.Fonts->Fonts[1];

    ImGui::PushID(label.c_str());

    const auto trackEdit = [this, entityID, &values, &onEditCommitted]() {
        const ImGuiID itemID = ImGui::GetItemID();
        if (ImGui::IsItemActivated())
            m_Vec3EditSnapshots[itemID] = { .EntityID = entityID, .Value = values };

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (const auto it = m_Vec3EditSnapshots.find(itemID); it != m_Vec3EditSnapshots.end()) {
                if (onEditCommitted && it->second.EntityID == entityID)
                    onEditCommitted(it->second.Value);
                m_Vec3EditSnapshots.erase(it);
            }
        }
    };

    const auto resetAxis = [&](float& axis) {
        const glm::vec3 before = values;
        axis = resetValue;
        if (onValueChanged)
            onValueChanged();
        if (onEditCommitted)
            onEditCommitted(before);
    };

    ImGui::Columns(2);
    ImGui::SetColumnWidth(0, columnWidth);
    ImGui::Text("%s", label.c_str());
    ImGui::NextColumn();

    ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    const float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
    const ImVec2 buttonSize = ImVec2(lineHeight + 2.0f, lineHeight);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.25f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.2f, 0.25f, 1.0f));

    ImGui::PushFont(boldFont);
    if (ImGui::Button("X", buttonSize)) {
        resetAxis(values.x);
    }
    ImGui::PopFont();

    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    /// ##X is used to hide the label of the input field
    if (ImGui::DragFloat("##X", &values.x, 0.1f)) {
        if (onValueChanged)
            onValueChanged();
    }
    trackEdit();

    ImGui::PopItemWidth();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.9f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.25f, 0.8f, 0.2f, 1.0f));

    ImGui::PushFont(boldFont);
    if (ImGui::Button("Y", buttonSize)) {
        resetAxis(values.y);
    }
    ImGui::PopFont();

    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    /// ##Y is used to hide the label of the input field
    if (ImGui::DragFloat("##Y", &values.y, 0.1f)) {
        if (onValueChanged)
            onValueChanged();
    }
    trackEdit();

    ImGui::PopItemWidth();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.9f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.25f, 0.8f, 1.0f));

    ImGui::PushFont(boldFont);
    if (ImGui::Button("Z", buttonSize)) {
        resetAxis(values.z);
    }
    ImGui::PopFont();

    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    /// ##Z is used to hide the label of the input field
    if (ImGui::DragFloat("##Z", &values.z, 0.1f)) {
        if (onValueChanged)
            onValueChanged();
    }
    trackEdit();
    ImGui::PopItemWidth();

    ImGui::PopStyleVar();

    /// Reset the column value to be the default
    ImGui::Columns(1);

    ImGui::PopID();
}

void HierarchyPanel::DrawComponents(const Entity entity)
{
    if (entity.HasComponent<TagComponent>()) {
        if (ImGui::TreeNodeEx(
                reinterpret_cast<void*>(typeid(TagComponent).hash_code()), ImGuiTreeNodeFlags_DefaultOpen, "Tag")) {
            auto& tag = entity.GetComponent<TagComponent>().Tag;

            char buffer[256];
            strcpy_s(buffer, sizeof(buffer), tag.c_str());
            if (ImGui::InputText("Tag", buffer, sizeof(buffer))) {
                tag = buffer;
            }

            if (ImGui::IsItemActivated()) {
                m_TagBeforeEdit = tag;
                m_TagEditEntity = entity.GetUUID();
                m_TagEditActive = true;
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                if (m_TagEditActive && m_TagEditEntity == entity.GetUUID() && m_UndoRedo) {
                    TagComponent before;
                    before.Tag = m_TagBeforeEdit;

                    TagComponent after;
                    after.Tag = tag;

                    m_UndoRedo->Record(std::make_unique<SetComponentValueCommand<TagComponent>>(
                        m_Context, entity.GetUUID(), before, after, "Rename Entity"));
                }
                m_TagEditActive = false;
            }

            ImGui::TreePop();
        }
    }
    if (entity.HasComponent<TransformComponent>()) {
        if (ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TransformComponent).hash_code()),
                              ImGuiTreeNodeFlags_DefaultOpen,
                              "Transform")) {
            auto& transform = entity.GetComponent<TransformComponent>();
            const auto onValueChanged = [&entity, this]() {
                if (entity.HasComponent<RigidBody3DComponent>()) {
                    entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
                }
                m_Context->CalculateEntityTransform(entity);
            };

            const auto makeCommit = [this, entity](glm::vec3 TransformComponent::*field, const char* commandName) {
                return [this, entity, field, commandName](const glm::vec3& before) {
                    if (!m_UndoRedo)
                        return;

                    const TransformComponent after = entity.GetComponent<TransformComponent>();
                    TransformComponent beforeComponent = after;
                    beforeComponent.*field = before;

                    m_UndoRedo->Record(std::make_unique<SetComponentValueCommand<TransformComponent>>(
                        m_Context, entity.GetUUID(), beforeComponent, after, commandName,
                        [scene = m_Context](const Entity target) {
                            if (scene)
                                scene->CalculateEntityTransform(target);
                        }));
                };
            };

            DrawVec3Control("Position", entity.GetUUID(), transform.Translation, 0.0f, 80.0f, onValueChanged,
                            makeCommit(&TransformComponent::Translation, "Transform Position"));
            DrawVec3Control("Rotation", entity.GetUUID(), transform.Rotation, 0.0f, 80.0f, onValueChanged,
                            makeCommit(&TransformComponent::Rotation, "Transform Rotation"));
            DrawVec3Control("Scale", entity.GetUUID(), transform.Scale, 1.0f, 80.0f, onValueChanged,
                            makeCommit(&TransformComponent::Scale, "Transform Scale"));

            ImGui::TreePop();
        }
    }

    constexpr ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap;

    if (entity.HasComponent<CameraComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CameraComponent).hash_code()), treeNodeFlags, "Camera");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Camera Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& cameraComponent = entity.GetComponent<CameraComponent>();
            auto& camera = cameraComponent.Camera;

            const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
            const char* currentProjectionTypeString =
                projectionTypeStrings[static_cast<int>(camera.GetProjectionType())];

            if (ImGui::BeginCombo("Projection Type", currentProjectionTypeString)) {
                for (int i = 0; i < 2; i++) {
                    const bool isSelected = (currentProjectionTypeString == projectionTypeStrings[i]);
                    if (ImGui::Selectable(projectionTypeStrings[i], isSelected)) {
                        camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(i));
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }

            if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic) {
                float size = camera.GetOrthographicSize();
                if (ImGui::DragFloat("Size", &size)) {
                    camera.SetOrthographicSize(size);
                }
                float nearClip = camera.GetOrthographicNearClip();
                if (ImGui::DragFloat("Near Clip", &nearClip)) {
                    camera.SetOrthographicNearClip(nearClip);
                }
                float farClip = camera.GetOrthographicFarClip();
                if (ImGui::DragFloat("Far Clip", &farClip)) {
                    camera.SetOrthographicFarClip(farClip);
                }
            }
            else {
                float fov = glm::degrees(camera.GetPerspectiveFov());
                if (ImGui::DragFloat("FOV", &fov)) {
                    camera.SetPerspectiveFov(glm::radians(fov));
                }
                float nearClip = camera.GetPerspectiveNearClip();
                if (ImGui::DragFloat("Near Clip", &nearClip)) {
                    camera.SetPerspectiveNearClip(nearClip);
                }
                float farClip = camera.GetPerspectiveFarClip();
                if (ImGui::DragFloat("Far Clip", &farClip)) {
                    camera.SetPerspectiveFarClip(farClip);
                }
            }

            ImGui::Checkbox("Primary", &cameraComponent.IsPrimary);
            ImGui::Checkbox("Fixed Aspect Ratio", &cameraComponent.FixedAspectRatio);

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<CameraComponent>();
        }
    }

    if (entity.HasComponent<ScriptComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<const void*>(typeid(ScriptComponent).hash_code()), treeNodeFlags, "Script");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Script Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& component = entity.GetComponent<ScriptComponent>();

            int scriptIndexToRemove = -1;
            for (int scriptIndex = 0; scriptIndex < static_cast<int>(component.ClassNames.size()); ++scriptIndex)
            {
                ImGui::PushID(scriptIndex);

                std::string& className = component.ClassNames[scriptIndex];
                bool scriptClassExists = false;
                const auto& entityClasses = ScriptEngine::GetEntityClasses();

                if (entityClasses.contains(className)) {
                    scriptClassExists = true;
                    ScriptEngine::CreateScriptFieldInitializers(entity, className);
                }

                char buffer[128];
                strcpy_s(buffer, sizeof(buffer), className.c_str());

                if (!scriptClassExists) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.3f, 1.0f));
                }

                ImGui::SetNextItemWidth(-90.0f);
                if (ImGui::InputText("##Class", buffer, sizeof(buffer))) {
                    if (entityClasses.contains(buffer)) {
                        ScriptEngine::CreateScriptFieldInitializers(entity, buffer);
                    }
                    className = buffer;
                }

                if (!scriptClassExists) {
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    scriptIndexToRemove = scriptIndex;
                }

                if (scriptClassExists) {
                    /// We decide whether to show the initializer fields based on whether the script instance exists
                    /// This is basically just a check if the scene is running or not,
                    /// since script instances are only created when the scene is running
                    if (const Ref<ScriptInstance>& scriptInstance = ScriptEngine::GetEntityInstance(entity.GetUUID(), className)) {
                        /// The scene is running, show the live fields for the script instance

                        /// Fields
                        const auto& fields = scriptInstance->GetScriptClass()->GetSerializedFields();
                        for (const auto& [name, scriptField] : fields) {
                            if (scriptField.Type == ScriptFieldType::Short) {
                                short value = scriptInstance->GetFieldValue<short>(name);
                                if (ImGui::InputScalar(name.c_str(), ImGuiDataType_S16, &value)) {
                                    scriptInstance->SetFieldValue<short>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Int) {
                                int value = scriptInstance->GetFieldValue<int>(name);
                                if (ImGui::InputInt(name.c_str(), &value)) {
                                    scriptInstance->SetFieldValue<int>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Long) {
                                int64_t value = scriptInstance->GetFieldValue<int64_t>(name);
                                if (ImGui::InputScalar(name.c_str(), ImGuiDataType_S64, &value)) {
                                    scriptInstance->SetFieldValue<int64_t>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::UShort) {
                                uint16_t value = scriptInstance->GetFieldValue<uint16_t>(name);
                                if (ImGui::InputScalar(name.c_str(), ImGuiDataType_U16, &value)) {
                                    scriptInstance->SetFieldValue<uint16_t>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::UInt) {
                                uint32_t value = scriptInstance->GetFieldValue<uint32_t>(name);
                                if (ImGui::InputScalar(name.c_str(), ImGuiDataType_U32, &value)) {
                                    scriptInstance->SetFieldValue<uint32_t>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::ULong) {
                                uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                                if (ImGui::InputScalar(name.c_str(), ImGuiDataType_U64, &value)) {
                                    scriptInstance->SetFieldValue<uint64_t>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Float) {
                                float value = scriptInstance->GetFieldValue<float>(name);
                                if (ImGui::DragFloat(name.c_str(), &value, 0.1f)) {
                                    scriptInstance->SetFieldValue<float>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Double) {
                                double value = scriptInstance->GetFieldValue<double>(name);
                                if (ImGui::DragScalar(name.c_str(), ImGuiDataType_Double, &value, 0.1f)) {
                                    scriptInstance->SetFieldValue<double>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Bool) {
                                bool value = scriptInstance->GetFieldValue<bool>(name);
                                if (ImGui::Checkbox(name.c_str(), &value)) {
                                    scriptInstance->SetFieldValue<bool>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::String) {
                                std::string value = scriptInstance->GetFieldValue<std::string>(name);
                                char strBuffer[256];
                                strcpy_s(strBuffer, sizeof(strBuffer), value.c_str());
                                if (ImGui::InputText(name.c_str(), strBuffer, sizeof(strBuffer))) {
                                    scriptInstance->SetFieldValue<std::string>(name, std::string(strBuffer));
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Vec2) {
                                glm::vec2 value = scriptInstance->GetFieldValue<glm::vec2>(name);
                                if (ImGui::DragFloat2(name.c_str(), glm::value_ptr(value), 0.1f)) {
                                    scriptInstance->SetFieldValue<glm::vec2>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Vec3) {
                                glm::vec3 value = scriptInstance->GetFieldValue<glm::vec3>(name);
                                if (ImGui::DragFloat3(name.c_str(), glm::value_ptr(value), 0.1f)) {
                                    scriptInstance->SetFieldValue<glm::vec3>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Vec4) {
                                glm::vec4 value = scriptInstance->GetFieldValue<glm::vec4>(name);
                                if (ImGui::DragFloat4(name.c_str(), glm::value_ptr(value), 0.1f)) {
                                    scriptInstance->SetFieldValue<glm::vec4>(name, value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::MaterialRef) {
                                const uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                                if (const auto newHandle = DrawScriptAssetReferenceField(
                                        name.c_str(), value, AssetType::Material, "Search materials...")) {
                                    scriptInstance->SetFieldValue<uint64_t>(
                                        name, static_cast<uint64_t>(*newHandle));
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::MeshRef) {
                                const uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                                if (const auto newHandle = DrawScriptAssetReferenceField(
                                        name.c_str(), value, AssetType::Mesh, "Search meshes...")) {
                                    scriptInstance->SetFieldValue<uint64_t>(
                                        name, static_cast<uint64_t>(*newHandle));
                                }
                            }
                        else if (scriptField.Type == ScriptFieldType::PrefabRef) {
                            const uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                            if (const auto newHandle = DrawScriptAssetReferenceField(
                                    name.c_str(), value, AssetType::Prefab)) {
                                scriptInstance->SetFieldValue<uint64_t>(
                                    name, static_cast<uint64_t>(*newHandle));
                            }
                        }
                        else if (scriptField.Type == ScriptFieldType::EntityRef) {
                            const uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                            if (const auto newEntityID = DrawScriptEntityReferenceField(
                                    name.c_str(), m_Context.get(), value)) {
                                scriptInstance->SetFieldValue<uint64_t>(name, *newEntityID);
                            }
                        }
                        else if (scriptField.Type == ScriptFieldType::TextureRef) {
                            const uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                            if (const auto newHandle = DrawScriptAssetReferenceField(
                                    name.c_str(), value, AssetType::Texture2D)) {
                                scriptInstance->SetFieldValue<uint64_t>(
                                    name, static_cast<uint64_t>(*newHandle));
                            }
                        }
                        else if (scriptField.Type == ScriptFieldType::AssetHandle) {
                            const uint64_t value = scriptInstance->GetFieldValue<uint64_t>(name);
                            if (const auto newHandle =
                                    DrawScriptAnyAssetReferenceField(name.c_str(), value)) {
                                scriptInstance->SetFieldValue<uint64_t>(
                                    name, static_cast<uint64_t>(*newHandle));
                            }
                        }
                            else {
                                ImGui::Text("Unsupported field type");
                            }
                        }
                    }
                    else {
                        /// The scene is not running, show the initializer fields for the script,
                        /// which then will be applied to the script when the scene starts.
                        auto& fields = ScriptEngine::GetScriptFieldInitializerMap(entity, className);
                        for (auto& [fieldName, fieldInitializer] : fields) {
                            const auto& scriptField = fieldInitializer.Field;

                            if (scriptField.Type == ScriptFieldType::Short) {
                                short value = fieldInitializer.GetValue<short>();
                                if (ImGui::InputScalar(fieldName.c_str(), ImGuiDataType_S16, &value)) {
                                    fieldInitializer.SetValue<short>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Int) {
                                int value = fieldInitializer.GetValue<int>();
                                if (ImGui::InputInt(fieldName.c_str(), &value)) {
                                    fieldInitializer.SetValue<int>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Long) {
                                int64_t value = fieldInitializer.GetValue<int64_t>();
                                if (ImGui::InputScalar(fieldName.c_str(), ImGuiDataType_S64, &value)) {
                                    fieldInitializer.SetValue<int64_t>(value);
                                }
                            }
                            if (scriptField.Type == ScriptFieldType::UShort) {
                                uint16_t value = fieldInitializer.GetValue<uint16_t>();
                                if (ImGui::InputScalar(fieldName.c_str(), ImGuiDataType_U16, &value)) {
                                    fieldInitializer.SetValue<uint16_t>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::UInt) {
                                uint32_t value = fieldInitializer.GetValue<uint32_t>();
                                if (ImGui::InputScalar(fieldName.c_str(), ImGuiDataType_U32, &value)) {
                                    fieldInitializer.SetValue<uint32_t>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::ULong) {
                                uint64_t value = fieldInitializer.GetValue<uint64_t>();
                                if (ImGui::InputScalar(fieldName.c_str(), ImGuiDataType_U64, &value)) {
                                    fieldInitializer.SetValue<uint64_t>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Float) {
                                float value = fieldInitializer.GetValue<float>();
                                if (ImGui::DragFloat(fieldName.c_str(), &value, 0.1f)) {
                                    fieldInitializer.SetValue<float>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Double) {
                                double value = fieldInitializer.GetValue<double>();
                                if (ImGui::DragScalar(fieldName.c_str(), ImGuiDataType_Double, &value, 0.1f)) {
                                    fieldInitializer.SetValue<double>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Bool) {
                                bool value = fieldInitializer.GetValue<bool>();
                                if (ImGui::Checkbox(fieldName.c_str(), &value)) {
                                    fieldInitializer.SetValue<bool>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::String) {
                                std::string value = fieldInitializer.GetValue<std::string>();
                                char strBuffer[256];
                                strcpy_s(strBuffer, sizeof(strBuffer), value.c_str());
                                if (ImGui::InputText(fieldName.c_str(), strBuffer, sizeof(strBuffer))) {
                                    fieldInitializer.SetValue<std::string>(std::string(strBuffer));
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Vec2) {
                                glm::vec2 value = fieldInitializer.GetValue<glm::vec2>();
                                if (ImGui::DragFloat2(fieldName.c_str(), glm::value_ptr(value), 0.1f)) {
                                    fieldInitializer.SetValue<glm::vec2>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Vec3) {
                                glm::vec3 value = fieldInitializer.GetValue<glm::vec3>();
                                if (ImGui::DragFloat3(fieldName.c_str(), glm::value_ptr(value), 0.1f)) {
                                    fieldInitializer.SetValue<glm::vec3>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::Vec4) {
                                glm::vec4 value = fieldInitializer.GetValue<glm::vec4>();
                                if (ImGui::DragFloat4(fieldName.c_str(), glm::value_ptr(value), 0.1f)) {
                                    fieldInitializer.SetValue<glm::vec4>(value);
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::MaterialRef) {
                                const uint64_t value = fieldInitializer.GetValue<uint64_t>();
                                if (const auto newHandle = DrawScriptAssetReferenceField(
                                        fieldName.c_str(), value, AssetType::Material, "Search materials...")) {
                                    fieldInitializer.SetValue(static_cast<uint64_t>(*newHandle));
                                }
                            }
                            else if (scriptField.Type == ScriptFieldType::MeshRef) {
                                const uint64_t value = fieldInitializer.GetValue<uint64_t>();
                                if (const auto newHandle = DrawScriptAssetReferenceField(
                                        fieldName.c_str(), value, AssetType::Mesh, "Search meshes...")) {
                                    fieldInitializer.SetValue(static_cast<uint64_t>(*newHandle));
                                }
                            }
                        else if (scriptField.Type == ScriptFieldType::PrefabRef) {
                            const uint64_t value = fieldInitializer.GetValue<uint64_t>();
                            if (const auto newHandle = DrawScriptAssetReferenceField(
                                    fieldName.c_str(), value, AssetType::Prefab)) {
                                fieldInitializer.SetValue(static_cast<uint64_t>(*newHandle));
                            }
                        }
                        else if (scriptField.Type == ScriptFieldType::EntityRef) {
                            const uint64_t value = fieldInitializer.GetValue<uint64_t>();
                            if (const auto newEntityID = DrawScriptEntityReferenceField(
                                    fieldName.c_str(), m_Context.get(), value)) {
                                fieldInitializer.SetValue<uint64_t>(*newEntityID);
                            }
                        }
                        else if (scriptField.Type == ScriptFieldType::TextureRef) {
                            const uint64_t value = fieldInitializer.GetValue<uint64_t>();
                            if (const auto newHandle = DrawScriptAssetReferenceField(
                                    fieldName.c_str(), value, AssetType::Texture2D)) {
                                fieldInitializer.SetValue(static_cast<uint64_t>(*newHandle));
                            }
                        }
                        else if (scriptField.Type == ScriptFieldType::AssetHandle) {
                            const uint64_t value = fieldInitializer.GetValue<uint64_t>();
                            if (const auto newHandle = DrawScriptAnyAssetReferenceField(
                                    fieldName.c_str(), value)) {
                                fieldInitializer.SetValue(static_cast<uint64_t>(*newHandle));
                            }
                        }
                            else {
                                ImGui::Text("Unsupported field type");
                            }
                        }
                    }
                }

                ImGui::Separator();
                ImGui::PopID();
            }

            if (ImGui::Button("Add Script")) {
                component.ClassNames.emplace_back();
            }

            if (scriptIndexToRemove >= 0 && scriptIndexToRemove < static_cast<int>(component.ClassNames.size())) {
                component.ClassNames.erase(component.ClassNames.begin() + scriptIndexToRemove);
            }

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<ScriptComponent>();
        }
    }

    if (entity.HasComponent<SpriteRendererComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(SpriteRendererComponent).hash_code()), treeNodeFlags, "Sprite");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Sprite Renderer Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& spriteRenderer = entity.GetComponent<SpriteRendererComponent>();
            ImGui::ColorEdit4("Color", &spriteRenderer.Color[0]);

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<SpriteRendererComponent>();
        }
    }
    if (entity.HasComponent<DirectionalLightComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(DirectionalLightComponent).hash_code()), treeNodeFlags, "Directional Light");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Directional Light Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& directionalLight = entity.GetComponent<DirectionalLightComponent>();
            ImGui::ColorEdit3("Color", &directionalLight.Light.Color[0]);
            if (ImGui::DragFloat("Intensity", &directionalLight.Light.Intensity, 1.0f, 0.0f)) {
                directionalLight.NeedsUpdate = true;
            }
            DrawVec3Control("Direction", entity.GetUUID(), directionalLight.Light.Direction, 0.0f, 80.0f,
                            [&directionalLight]() { directionalLight.NeedsUpdate = true; });
            ImGui::Checkbox("Enabled", &directionalLight.IsEnabled);
            ImGui::Checkbox("Cast Shadows", &directionalLight.CastShadows);

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<DirectionalLightComponent>();
        }
    }
    if (entity.HasComponent<PointLightComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(PointLightComponent).hash_code()), treeNodeFlags, "Point Light");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Point Light Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& pointLight = entity.GetComponent<PointLightComponent>();
            ImGui::Checkbox("Enabled", &pointLight.IsEnabled);
            ImGui::ColorEdit3("Color", &pointLight.Light.Color[0]);
            ImGui::DragFloat("Intensity", &pointLight.Light.Intensity, 1.0f, 0.0f);
            ImGui::DragFloat("Radius", &pointLight.Light.Radius, 1.0f, 0.0f);

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<PointLightComponent>();
        }
    }
    if (entity.HasComponent<SpotLightComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(SpotLightComponent).hash_code()), treeNodeFlags, "Spotlight");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Spotlight Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& spotlight = entity.GetComponent<SpotLightComponent>();
            ImGui::ColorEdit3("Color", &spotlight.Light.Color[0]);
            ImGui::DragFloat("Intensity", &spotlight.Light.Intensity, 1.0f, 0.0f, 10.0f);
            ImGui::DragFloat("Inner Cone Cosine Radians", &spotlight.Light.CutOffAngleRadians, 0.01f, 0.0f);
            ImGui::DragFloat("Outer Cone Cosine Radians", &spotlight.Light.OuterCutOffAngleRadians, 0.01f, 0.0f);
            ImGui::Checkbox("Enabled", &spotlight.IsEnabled);

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<SpotLightComponent>();
        }
    }
    if (entity.HasComponent<StaticMeshComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(StaticMeshComponent).hash_code()), treeNodeFlags, "Static Mesh");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Static Mesh Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& staticMesh = entity.GetComponent<StaticMeshComponent>();
            ImGui::Checkbox("Visible", &staticMesh.Visible);

            AssetPicker::DrawAsset("Static Mesh", AssetType::Mesh, staticMesh.StaticMesh, "Search meshes...");
            AssetPicker::DrawAsset("Static Material", AssetType::Material, staticMesh.MeshMaterial, "Search materials...");

            ImGui::Separator();

            ImGui::Checkbox("Cast Shadows", &staticMesh.CastShadows);

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<StaticMeshComponent>();
        }
    }
    if (entity.HasComponent<RigidBody3DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(RigidBody3DComponent).hash_code()), treeNodeFlags, "RigidBody3D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("RigidBody3D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& rb = entity.GetComponent<RigidBody3DComponent>();
            bool rbChanged = false;

            const char* bodyTypeStrings[] = { "Static", "Dynamic", "Kinematic" };
            const char* currentBodyTypeString = bodyTypeStrings[static_cast<int>(rb.Type)];
            if (ImGui::BeginCombo("Body Type", currentBodyTypeString)) {
                for (int i = 0; i < 3; i++) {
                    const bool isSelected = (currentBodyTypeString == bodyTypeStrings[i]);
                    if (ImGui::Selectable(bodyTypeStrings[i], isSelected)) {
                        rb.Type = static_cast<RigidBody3DComponent::BodyType>(i);
                        rbChanged = true;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            rbChanged |= ImGui::DragFloat("Friction", &rb.Friction, 0.02f, 0.0f, 1.0f);
            rbChanged |= ImGui::DragFloat("Restitution", &rb.Restitution, 0.02f, 0.0f, 1.0f);
            rbChanged |= ImGui::DragFloat3("Velocity", glm::value_ptr(rb.Velocity), 0.1f);
            rbChanged |= ImGui::DragFloat3("Angular Velocity", glm::value_ptr(rb.AngularVelocity));
            rbChanged |= ImGui::Checkbox("Use Gravity", &rb.UseGravity);
            rbChanged |= ImGui::DragFloat("Mass", &rb.Mass, 0.1f, 0.01f, 100.0f);
            if (rbChanged) {
                rb.IsDirty = true;
            }

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<RigidBody3DComponent>();
        }
    }
    if (entity.HasComponent<BoxCollider3DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(BoxCollider3DComponent).hash_code()), treeNodeFlags, "BoxCollider3D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("BoxCollider3D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& collider = entity.GetComponent<BoxCollider3DComponent>();
            bool colliderChanged = false;
            colliderChanged |= ImGui::DragFloat3("Size", glm::value_ptr(collider.Size), 0.1f);
            colliderChanged |= ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
            colliderChanged |= ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
            if (colliderChanged && entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }

            ImGui::TreePop();
        }

        if (componentDeleted) {
            if (entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            entity.RemoveComponent<BoxCollider3DComponent>();
        }
    }
    if (entity.HasComponent<SphereCollider3DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(SphereCollider3DComponent).hash_code()), treeNodeFlags, "SphereCollider3D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("SphereCollider3D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& collider = entity.GetComponent<SphereCollider3DComponent>();
            bool colliderChanged = false;
            colliderChanged |= ImGui::DragFloat("Radius", &collider.Radius, 0.1f);
            colliderChanged |= ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
            colliderChanged |= ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
            if (colliderChanged && entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            ImGui::TreePop();
        }
        if (componentDeleted) {
            if (entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            entity.RemoveComponent<SphereCollider3DComponent>();
        }
    }
    if (entity.HasComponent<CapsuleCollider3DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(CapsuleCollider3DComponent).hash_code()),
                                              treeNodeFlags,
                                              "CapsuleCollider3D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("CapsuleCollider3D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& collider = entity.GetComponent<CapsuleCollider3DComponent>();
            bool colliderChanged = false;
            colliderChanged |= ImGui::DragFloat("Radius", &collider.Radius, 0.1f);
            colliderChanged |= ImGui::DragFloat("Height", &collider.Height, 0.1f);
            colliderChanged |= ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
            colliderChanged |= ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
            if (colliderChanged && entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            ImGui::TreePop();
        }
        if (componentDeleted) {
            if (entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            entity.RemoveComponent<CapsuleCollider3DComponent>();
        }
    }
    if (entity.HasComponent<MeshCollider3DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(MeshCollider3DComponent).hash_code()), treeNodeFlags, "MeshCollider3D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("MeshCollider3D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& collider = entity.GetComponent<MeshCollider3DComponent>();
            bool colliderChanged = false;
            colliderChanged |= ImGui::DragFloat3("Offset", glm::value_ptr(collider.Offset), 0.1f);
            colliderChanged |= ImGui::Checkbox("Is Trigger", &collider.IsTrigger);
            if (colliderChanged && entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            std::string meshLabel = "None";
            if (collider.Mesh) {
                if (AssetManager::IsAssetHandleValid(collider.Mesh->GetHandle())) {
                    const auto& metadata =
                        Project::GetActive()->GetEditorAssetManager()->GetMetadata(collider.Mesh->GetHandle());
                    meshLabel = metadata.Filepath.filename().string();
                }
                else {
                    meshLabel = "Invalid Mesh";
                }
            }
            ImGui::Text("Mesh: %s", meshLabel.c_str());
            ImGui::Separator();
            // if (ImGui::Button("Select Mesh"))
            //{
            //	m_Context->SetSelectedEntity(entity);
            //	m_Context->OpenAssetBrowser(AssetType::Mesh);
            // }
            ImGui::TreePop();
        }
        if (componentDeleted) {
            if (entity.HasComponent<RigidBody3DComponent>()) {
                entity.GetComponent<RigidBody3DComponent>().IsDirty = true;
            }
            entity.RemoveComponent<MeshCollider3DComponent>();
        }
    }
    if (entity.HasComponent<EnvironmentComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(EnvironmentComponent).hash_code()), treeNodeFlags, "Environment");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Environment Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& environment = entity.GetComponent<EnvironmentComponent>();
            ImGui::Checkbox("Skybox Enabled", &environment.IsSkyboxEnabled);

            /// Render the environment cubemap into an image
            ImGui::Text("Skybox Texture");
            /*const uint64_t textureID = m_CubemapFramebuffer->GetColorAttachmentRendererID();
            ImGui::Image(textureID, ImVec2{ 256, 256 }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });*/

            if (AssetPicker::DrawAssetHandle("Cubemap", AssetType::TextureCube, environment.SkyboxTexture,
                    "Search cubemaps..."))
            {
                /// render the changed cube into the environment
                /*m_CubemapFramebuffer->Bind();
                m_CubemapFramebuffer->Unbind();*/
            }

            ImGui::TreePop();
        }
        if (componentDeleted) {
            entity.RemoveComponent<EnvironmentComponent>();
        }
    }
    if (entity.HasComponent<TextComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened =
            ImGui::TreeNodeEx(reinterpret_cast<void*>(typeid(TextComponent).hash_code()), treeNodeFlags, "Text");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Text Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& text = entity.GetComponent<TextComponent>();

            char buffer[256];
            strcpy_s(buffer, sizeof(buffer), text.Text.c_str());
            if (ImGui::InputText("Text", buffer, sizeof(buffer))) {
                text.Text = buffer;
            }

            ImGui::Separator();

            ImGui::DragFloat4("Color", &text.Color[0], 0.01f, 0.0f, 1.0f);

            ImGui::Separator();

            ImGui::DragFloat("FontSize", &text.FontSize);

            ImGui::Separator();

            ImGui::Text("Font: %s", text.Font->GetName().c_str());
            const uint64_t fontAtlasRendererID = VulkanContext::Get().GetImGuiRendererID(text.Font->GetAtlasTexture());
            ImGui::Image(fontAtlasRendererID, ImVec2{ 128, 128 }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });

            /// Handle drag and drop for fonts
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(assetBrowserFont)) {
                    const AssetHandle handle = ReadAssetDragPayload(payload->Data, payload->DataSize).Handle;
                    if (AssetManager::GetAssetType(handle) !=
                        AssetType::Texture2D) // TODO: Change to font type when it exists
                    {
                        Log::EditorError("Asset is not a texture: {0}", handle);
                        m_NotificationManager.AddNotification("Asset is not a font", Notification::Type::Error);
                        return;
                    }
                    // text.Font = AssetManager::GetAsset<Font>(handle);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TreePop();
        }
        if (componentDeleted) {
            entity.RemoveComponent<EnvironmentComponent>();
        }
    }
    if (entity.HasComponent<AudioSource2DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(AudioSource2DComponent).hash_code()), treeNodeFlags, "Audio Source 2D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Audio Source 2D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& audioComp = entity.GetComponent<AudioSource2DComponent>();

            AssetPicker::DrawAsset("Audio Clip 2D", AssetType::Sound, audioComp.SoundAsset, "Search audio...");

            ImGui::Separator();

            ImGui::Checkbox("Loop", &audioComp.Loop);

            ImGui::DragFloat("Volume", &audioComp.Volume, 0.01f, 0.0f, 1.0f);

            ImGui::TreePop();
        }
        if (componentDeleted) {
            entity.RemoveComponent<AudioSource2DComponent>();
        }
    }
    if (entity.HasComponent<AudioSource3DComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(AudioSource3DComponent).hash_code()), treeNodeFlags, "Audio Source 3D");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Audio Source 3D Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& audioComp = entity.GetComponent<AudioSource3DComponent>();

            AssetPicker::DrawAsset("Audio Clip 3D", AssetType::Sound, audioComp.SoundAsset, "Search audio...");

            ImGui::Separator();

            ImGui::Checkbox("Loop", &audioComp.Loop);

            ImGui::DragFloat("Volume", &audioComp.Volume, 0.01f, 0.0f, 1.0f);

            ImGui::TreePop();
        }
        if (componentDeleted) {
            entity.RemoveComponent<AudioSource3DComponent>();
        }
    }
    if (entity.HasComponent<AudioListenerComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(AudioListenerComponent).hash_code()), treeNodeFlags, "Audio Listener");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Audio Listener Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened) {
            auto& listenerCOmp = entity.GetComponent<AudioListenerComponent>();

            ImGui::DragFloat("Volume", &listenerCOmp.Volume, 0.01f, 0.0f, 1.0f);

            ImGui::TreePop();
        }
        if (componentDeleted) {
            entity.RemoveComponent<AudioListenerComponent>();
        }
    }
    if (entity.HasComponent<ParticleEmitterComponent>()) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(ParticleEmitterComponent).hash_code()), treeNodeFlags, "Particle Emitter");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 })) {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings")) {
            ImGui::Text("Particle Emitter Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component")) {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened) {
            auto& particleEmitter = entity.GetComponent<ParticleEmitterComponent>();
            ImGui::Checkbox("Active", &particleEmitter.IsActive);
            ImGui::DragFloat("Spawn Rate", &particleEmitter.SpawnRate, 1.0f, 0.0f);

            float* lifetimeRange = &particleEmitter.MinLifetime;
            ImGui::DragFloat2("Lifetime Range", lifetimeRange, 0.1f, 0.0f);

            ImGui::DragFloat3("Velocity Range Min", glm::value_ptr(particleEmitter.MinVelocity), 0.1f);
            ImGui::DragFloat3("Velocity Range Max", glm::value_ptr(particleEmitter.MaxVelocity), 0.1f);

            ImGui::DragFloat3("Acceleration Range Min", glm::value_ptr(particleEmitter.MinAcceleration), 0.1f);
            ImGui::DragFloat3("Acceleration Range Max", glm::value_ptr(particleEmitter.MaxAcceleration), 0.1f);

            ImGui::ColorEdit4("Start Color", &particleEmitter.StartColor[0]);
            ImGui::ColorEdit4("End Color", &particleEmitter.EndColor[0]);

            float* sizeRange = &particleEmitter.StartSize;
            ImGui::DragFloat2("Size Range", sizeRange, 0.1f, 0.0f);

            ImGui::DragFloat2("Sub UV Grid", glm::value_ptr(particleEmitter.SubUVGrid), 1, 1, 16, "%f");

            AssetPicker::DrawAssetHandle("Texture", AssetType::Texture2D, particleEmitter.ParticleTexture,
                "Search textures...");

            ImGui::TreePop();
        }

        if (componentDeleted) {
            entity.RemoveComponent<ParticleEmitterComponent>();
        }
    }
    if (entity.HasComponent<SkeletonComponent>())
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(SkeletonComponent).hash_code()), treeNodeFlags, "Skeleton");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 }))
        {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            ImGui::Text("Skeleton Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component"))
            {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened)
        {
            auto& skeletonComp = entity.GetComponent<SkeletonComponent>();

            AssetHandle skeletonAsset = skeletonComp.SkeletonAsset;
            if (AssetPicker::DrawAssetHandle("Skeleton Asset", AssetType::Skeleton, skeletonAsset,
                    "Search skeletons..."))
            {
                skeletonComp.SkeletonAsset = skeletonAsset;
                skeletonComp.Skeleton = skeletonAsset.IsValid()
                    ? AssetManager::AsAsset<Skeleton>(skeletonAsset) : nullptr;
            }

            if (skeletonComp.Skeleton)
            {
                const auto& joints = skeletonComp.Skeleton->GetJoints();
                ImGui::Text("Joints: %zu", joints.size());
                if (ImGui::TreeNode("Joint List"))
                {
                    for (size_t i = 0; i < joints.size(); ++i)
                        ImGui::Text("%zu: %s", i, joints[i].Name.c_str());
                    ImGui::TreePop();
                }
            }
            else if (skeletonComp.SkeletonAsset.IsValid())
            {
                ImGui::TextDisabled("Skeleton asset could not be loaded.");
            }

            if (!skeletonComp.JointEntities.empty())
            {
                if (ImGui::TreeNode("Joint Entities"))
                {
                    for (size_t i = 0; i < skeletonComp.JointEntities.size(); ++i)
                    {
                        const Entity jointEntity = m_Context
                            ? m_Context->TryGetEntityByUUID(skeletonComp.JointEntities[i]) : Entity{};
                        ImGui::Text("%zu: %s", i, jointEntity
                            ? std::string(jointEntity.GetName()).c_str() : "Missing entity");
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::TreePop();
        }
        if (componentDeleted)
        {
            entity.RemoveComponent<SkeletonComponent>();
        }
    }
    if (entity.HasComponent<SkinnedMeshComponent>())
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(SkinnedMeshComponent).hash_code()), treeNodeFlags, "Skinned Mesh");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 }))
        {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            ImGui::Text("Skinned Mesh Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component"))
            {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened)
        {
            auto& skinnedMeshComp = entity.GetComponent<SkinnedMeshComponent>();

            AssetHandle meshAsset = skinnedMeshComp.MeshAsset;
            if (AssetPicker::DrawAssetHandle("Skinned Mesh Asset", AssetType::SkinnedMesh, meshAsset,
                    "Search skinned meshes..."))
            {
                skinnedMeshComp.MeshAsset = meshAsset;
                skinnedMeshComp.Mesh = meshAsset.IsValid()
                    ? AssetManager::AsAsset<SkeletalMesh>(meshAsset) : nullptr;
            }

            AssetHandle skeletonAsset = skinnedMeshComp.SkeletonAsset;
            if (AssetPicker::DrawAssetHandle("Skinned Skeleton Asset", AssetType::Skeleton, skeletonAsset,
                    "Search skeletons..."))
            {
                skinnedMeshComp.SkeletonAsset = skeletonAsset;
            }

            if (const auto newSkeletonEntity = DrawComponentEntityReferenceField<SkeletonComponent>(
                    "Skinned Skeleton Entity", m_Context, skinnedMeshComp.SkeletonEntity))
            {
                skinnedMeshComp.SkeletonEntity = *newSkeletonEntity;
                if (newSkeletonEntity->IsValid())
                {
                    const Entity skeletonEntity = m_Context->TryGetEntityByUUID(*newSkeletonEntity);
                    if (skeletonEntity && skeletonEntity.HasComponent<SkeletonComponent>())
                        skinnedMeshComp.SkeletonAsset =
                            skeletonEntity.GetComponent<SkeletonComponent>().SkeletonAsset;
                }
            }

            AssetHandle materialAsset = skinnedMeshComp.MaterialAsset;
            if (AssetPicker::DrawAssetHandle("Skinned Material", AssetType::Material, materialAsset,
                    "Search materials..."))
            {
                skinnedMeshComp.MaterialAsset = materialAsset;
                skinnedMeshComp.Material = materialAsset.IsValid()
                    ? AssetManager::AsAsset<Material>(materialAsset) : nullptr;
            }

            ImGui::Checkbox("Visible", &skinnedMeshComp.Visible);
            ImGui::Checkbox("Cast Shadows", &skinnedMeshComp.CastShadows);

            if (skinnedMeshComp.MeshAsset.IsValid() && !skinnedMeshComp.Mesh)
                ImGui::TextDisabled("Skinned mesh artifact could not be loaded.");
            if (skinnedMeshComp.MaterialAsset.IsValid() && !skinnedMeshComp.Material)
                ImGui::TextDisabled("Material artifact could not be loaded.");

            ImGui::TreePop();
        }
        if (componentDeleted)
        {
            entity.RemoveComponent<SkinnedMeshComponent>();
        }
    }
    if (entity.HasComponent<JointComponent>())
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(JointComponent).hash_code()), treeNodeFlags, "Joint");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 }))
        {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();
        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            ImGui::Text("Joint Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component"))
            {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (opened)
        {
            auto& jointComp = entity.GetComponent<JointComponent>();
            ImGui::Text("Joint Index: %u", jointComp.JointIndex);
            ImGui::TextDisabled("Joint indices are assigned by the imported skeleton.");
            ImGui::TreePop();
        }
        if (componentDeleted)
        {
            entity.RemoveComponent<JointComponent>();
        }
    }
    if (entity.HasComponent<AnimatorComponent>())
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        const bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(typeid(AnimatorComponent).hash_code()), treeNodeFlags, "Animator");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.f);
        if (ImGui::Button("+", ImVec2{ 20, 20 }))
        {
            ImGui::OpenPopup("ComponentSettings");
        }
        ImGui::PopStyleVar();

        bool componentDeleted = false;
        if (ImGui::BeginPopup("ComponentSettings"))
        {
            ImGui::Text("Animator Settings");
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Component"))
            {
                componentDeleted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (opened)
        {
            auto& animator = entity.GetComponent<AnimatorComponent>();

            if (const auto newSkeletonEntity = DrawComponentEntityReferenceField<SkeletonComponent>(
                    "Animator Skeleton Entity", m_Context, animator.SkeletonEntity))
            {
                animator.SkeletonEntity = *newSkeletonEntity;
            }

            AssetHandle clipToAdd = AssetHandle::Invalid();
            if (AssetPicker::DrawAssetHandle("Add Animation Clip", AssetType::Animation, clipToAdd,
                    "Search animation clips...", false))
            {
                const bool alreadyPresent = std::ranges::find(animator.AnimationClips, clipToAdd)
                    != animator.AnimationClips.end();
                if (!alreadyPresent)
                {
                    animator.AnimationClips.push_back(clipToAdd);
                    if (animator.ActiveClip < 0)
                        animator.ActiveClip = static_cast<int32_t>(animator.AnimationClips.size() - 1);
                }
                else
                {
                    const auto it = std::ranges::find(animator.AnimationClips, clipToAdd);
                    animator.ActiveClip = static_cast<int32_t>(std::distance(animator.AnimationClips.begin(), it));
                }
            }

            ImGui::Text("Animation Clips (%zu)", animator.AnimationClips.size());
            if (animator.AnimationClips.empty())
            {
                ImGui::TextDisabled("No clips assigned. Use Add Clip above.");
            }

            int32_t clipToRemove = -1;
            for (int32_t i = 0; i < static_cast<int32_t>(animator.AnimationClips.size()); ++i)
            {
                ImGui::PushID(i);
                const bool selected = animator.ActiveClip == i;
                if (ImGui::Selectable(GetAssetStemLabel(animator.AnimationClips[i]).c_str(), selected))
                    animator.ActiveClip = i;
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    clipToRemove = i;
                }
                ImGui::PopID();
            }

            if (clipToRemove >= 0)
            {
                animator.AnimationClips.erase(animator.AnimationClips.begin() + clipToRemove);
                if (animator.AnimationClips.empty())
                    animator.ActiveClip = -1;
                else if (animator.ActiveClip >= static_cast<int32_t>(animator.AnimationClips.size()))
                    animator.ActiveClip = static_cast<int32_t>(animator.AnimationClips.size() - 1);
            }

            if (!animator.AnimationClips.empty())
            {
                std::string activeClipLabel = animator.ActiveClip >= 0
                    && animator.ActiveClip < static_cast<int32_t>(animator.AnimationClips.size())
                    ? GetAssetStemLabel(animator.AnimationClips[static_cast<size_t>(animator.ActiveClip)])
                    : "None";
                if (ImGui::BeginCombo("Active Clip", activeClipLabel.c_str()))
                {
                    for (int32_t i = 0; i < static_cast<int32_t>(animator.AnimationClips.size()); ++i)
                    {
                        ImGui::PushID(i);
                        const bool selected = animator.ActiveClip == i;
                        if (ImGui::Selectable(GetAssetStemLabel(animator.AnimationClips[i]).c_str(), selected))
                            animator.ActiveClip = i;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }

            Ref<AnimationClip> activeClip = nullptr;
            if (animator.ActiveClip >= 0
                && animator.ActiveClip < static_cast<int32_t>(animator.AnimationClips.size()))
            {
                activeClip = AssetManager::AsAsset<AnimationClip>(
                    animator.AnimationClips[static_cast<size_t>(animator.ActiveClip)]);
            }

            const float duration = activeClip ? activeClip->GetDuration() : 0.0f;
            if (duration > 0.0f)
                ImGui::SliderFloat("Time", &animator.CurrentTime, 0.0f, duration, "%.3f s");
            else
                ImGui::DragFloat("Time", &animator.CurrentTime, 0.01f, 0.0f, 1000.0f, "%.3f s");
            ImGui::DragFloat("Playback Speed", &animator.PlaybackSpeed, 0.01f, 0.0f, 10.0f);
            ImGui::Checkbox("Playing", &animator.IsPlaying);
            ImGui::SameLine();
            ImGui::Checkbox("Looping", &animator.IsLooping);
            if (ImGui::Button("Reset Time"))
                animator.CurrentTime = 0.0f;

            if (activeClip)
                ImGui::Text("Clip Duration: %.3f s", duration);
            else if (!animator.AnimationClips.empty())
                ImGui::TextDisabled("Active clip could not be loaded.");

            ImGui::TreePop();
        }

        if (componentDeleted)
        {
            entity.RemoveComponent<AnimatorComponent>();
        }
    }
}

}
