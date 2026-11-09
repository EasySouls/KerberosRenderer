#pragma once

#include "Assets/Asset.hpp"

#include <imgui/imgui.h>

#include <cstdint>

namespace Kerberos
{
    /// Editor-only icon glyphs. Each entry maps to a PNG in
    /// Assets/Editor/Icons with the same snake_case file name.
#define KBR_EDITOR_ICON_LIST(X) \
    X(AssetTexture2d, "asset_texture2d") \
    X(AssetTexturecube, "asset_texturecube") \
    X(AssetMaterial, "asset_material") \
    X(AssetMesh, "asset_mesh") \
    X(AssetScene, "asset_scene") \
    X(AssetSound, "asset_sound") \
    X(AssetPrefab, "asset_prefab") \
    X(AssetSkeleton, "asset_skeleton") \
    X(AssetSkinnedmesh, "asset_skinnedmesh") \
    X(AssetAnimation, "asset_animation") \
    X(Folder, "folder") \
    X(FolderOpen, "folder_open") \
    X(File, "file") \
    X(FileUnknown, "file_unknown") \
    X(FileMeta, "file_meta") \
    X(Project, "project") \
    X(Script, "script") \
    X(Csproj, "csproj") \
    X(Shader, "shader") \
    X(Font, "font") \
    X(SceneManifest, "scene_manifest") \
    X(PrefabArtifact, "prefab_artifact") \
    X(ComponentTransform, "component_transform") \
    X(ComponentCamera, "component_camera") \
    X(ComponentScript, "component_script") \
    X(ComponentSprite, "component_sprite") \
    X(ComponentStaticMesh, "component_static_mesh") \
    X(ComponentText, "component_text") \
    X(ComponentEnvironment, "component_environment") \
    X(ComponentPrefab, "component_prefab") \
    X(ComponentModel, "component_model") \
    X(ComponentTag, "component_tag") \
    X(ComponentId, "component_id") \
    X(ComponentHierarchy, "component_hierarchy") \
    X(ComponentDirectionalLight, "component_directional_light") \
    X(ComponentPointLight, "component_point_light") \
    X(ComponentSpotLight, "component_spot_light") \
    X(ComponentSkeleton, "component_skeleton") \
    X(ComponentSkinnedMesh, "component_skinned_mesh") \
    X(ComponentAnimator, "component_animator") \
    X(ComponentJoint, "component_joint") \
    X(ComponentSocket, "component_socket") \
    X(ComponentRagdoll, "component_ragdoll") \
    X(ComponentRigidbody, "component_rigidbody") \
    X(ComponentBoxCollider, "component_box_collider") \
    X(ComponentSphereCollider, "component_sphere_collider") \
    X(ComponentCapsuleCollider, "component_capsule_collider") \
    X(ComponentMeshCollider, "component_mesh_collider") \
    X(ComponentAudio2d, "component_audio_2d") \
    X(ComponentAudio3d, "component_audio_3d") \
    X(ComponentAudioListener, "component_audio_listener") \
    X(ComponentParticles, "component_particles") \
    X(ComponentAiBlackboard, "component_ai_blackboard") \
    X(ComponentAiStateMachine, "component_ai_state_machine") \
    X(ComponentAiController, "component_ai_controller") \
    X(ComponentAiPerception, "component_ai_perception") \
    X(ComponentAiTargetable, "component_ai_targetable") \
    X(ComponentAiAgent, "component_ai_agent") \
    X(PanelAssets, "panel_assets") \
    X(PanelHierarchy, "panel_hierarchy") \
    X(PanelProperties, "panel_properties") \
    X(PanelViewport, "panel_viewport") \
    X(PanelToolbar, "panel_toolbar") \
    X(PanelConsole, "panel_console") \
    X(PanelAnimationEditor, "panel_animation_editor") \
    X(PanelDebug, "panel_debug") \
    X(TransportPlay, "transport_play") \
    X(TransportPause, "transport_pause") \
    X(TransportResume, "transport_resume") \
    X(TransportStop, "transport_stop") \
    X(TransportSimulate, "transport_simulate") \
    X(GizmoSelect, "gizmo_select") \
    X(GizmoTranslate, "gizmo_translate") \
    X(GizmoRotate, "gizmo_rotate") \
    X(GizmoScale, "gizmo_scale") \
    X(StateEdit, "state_edit") \
    X(StatePlay, "state_play") \
    X(StateSimulate, "state_simulate") \
    X(StatePaused, "state_paused") \
    X(ActionNew, "action_new") \
    X(ActionOpen, "action_open") \
    X(ActionSave, "action_save") \
    X(ActionSaveAs, "action_save_as") \
    X(ActionUndo, "action_undo") \
    X(ActionRedo, "action_redo") \
    X(ActionCut, "action_cut") \
    X(ActionCopy, "action_copy") \
    X(ActionPaste, "action_paste") \
    X(ActionDuplicate, "action_duplicate") \
    X(ActionDelete, "action_delete") \
    X(ActionImport, "action_import") \
    X(ActionRefresh, "action_refresh") \
    X(ActionReimport, "action_reimport") \
    X(ActionReveal, "action_reveal") \
    X(ActionSearch, "action_search") \
    X(ActionSettings, "action_settings") \
    X(ActionClear, "action_clear") \
    X(ActionAdd, "action_add") \
    X(ActionRemove, "action_remove") \
    X(ActionExit, "action_exit") \
    X(ActionFullscreen, "action_fullscreen") \
    X(ActionReload, "action_reload") \
    X(ActionWireframe, "action_wireframe") \
    X(ActionClose, "action_close") \
    X(NotificationInfo, "notification_info") \
    X(NotificationWarning, "notification_warning") \
    X(NotificationError, "notification_error") \
    X(LogTrace, "log_trace") \
    X(LogDebug, "log_debug") \
    X(LogInfo, "log_info") \
    X(LogWarning, "log_warning") \
    X(LogError, "log_error") \
    X(LogCritical, "log_critical") \
    X(StatusReady, "status_ready") \
    X(StatusMissing, "status_missing") \
    X(StatusNotImported, "status_not_imported") \
    X(StatusRegistered, "status_registered") \
    X(SlotAlbedo, "slot_albedo") \
    X(SlotMetallic, "slot_metallic") \
    X(SlotRoughness, "slot_roughness") \
    X(SlotNormal, "slot_normal") \
    X(SlotOcclusion, "slot_occlusion") \
    X(SlotEmissive, "slot_emissive") \
    X(SlotUnset, "slot_unset") \
    X(HierarchyEntity, "hierarchy_entity") \
    X(HierarchyChild, "hierarchy_child") \
    X(VisibilityOn, "visibility_on") \
    X(VisibilityOff, "visibility_off") \
    X(Lock, "lock") \
    X(Unlock, "unlock") \
    X(Expanded, "expanded") \
    X(Collapsed, "collapsed")

    enum class EditorIcon : std::uint8_t
    {
        AssetTexture2d,
        AssetTexturecube,
        AssetMaterial,
        AssetMesh,
        AssetScene,
        AssetSound,
        AssetPrefab,
        AssetSkeleton,
        AssetSkinnedmesh,
        AssetAnimation,
        Folder,
        FolderOpen,
        File,
        FileUnknown,
        FileMeta,
        Project,
        Script,
        Csproj,
        Shader,
        Font,
        SceneManifest,
        PrefabArtifact,
        ComponentTransform,
        ComponentCamera,
        ComponentScript,
        ComponentSprite,
        ComponentStaticMesh,
        ComponentText,
        ComponentEnvironment,
        ComponentPrefab,
        ComponentModel,
        ComponentTag,
        ComponentId,
        ComponentHierarchy,
        ComponentDirectionalLight,
        ComponentPointLight,
        ComponentSpotLight,
        ComponentSkeleton,
        ComponentSkinnedMesh,
        ComponentAnimator,
        ComponentJoint,
        ComponentSocket,
        ComponentRagdoll,
        ComponentRigidbody,
        ComponentBoxCollider,
        ComponentSphereCollider,
        ComponentCapsuleCollider,
        ComponentMeshCollider,
        ComponentAudio2d,
        ComponentAudio3d,
        ComponentAudioListener,
        ComponentParticles,
        ComponentAiBlackboard,
        ComponentAiStateMachine,
        ComponentAiController,
        ComponentAiPerception,
        ComponentAiTargetable,
        ComponentAiAgent,
        PanelAssets,
        PanelHierarchy,
        PanelProperties,
        PanelViewport,
        PanelToolbar,
        PanelConsole,
        PanelAnimationEditor,
        PanelDebug,
        TransportPlay,
        TransportPause,
        TransportResume,
        TransportStop,
        TransportSimulate,
        GizmoSelect,
        GizmoTranslate,
        GizmoRotate,
        GizmoScale,
        StateEdit,
        StatePlay,
        StateSimulate,
        StatePaused,
        ActionNew,
        ActionOpen,
        ActionSave,
        ActionSaveAs,
        ActionUndo,
        ActionRedo,
        ActionCut,
        ActionCopy,
        ActionPaste,
        ActionDuplicate,
        ActionDelete,
        ActionImport,
        ActionRefresh,
        ActionReimport,
        ActionReveal,
        ActionSearch,
        ActionSettings,
        ActionClear,
        ActionAdd,
        ActionRemove,
        ActionExit,
        ActionFullscreen,
        ActionReload,
        ActionWireframe,
        ActionClose,
        NotificationInfo,
        NotificationWarning,
        NotificationError,
        LogTrace,
        LogDebug,
        LogInfo,
        LogWarning,
        LogError,
        LogCritical,
        StatusReady,
        StatusMissing,
        StatusNotImported,
        StatusRegistered,
        SlotAlbedo,
        SlotMetallic,
        SlotRoughness,
        SlotNormal,
        SlotOcclusion,
        SlotEmissive,
        SlotUnset,
        HierarchyEntity,
        HierarchyChild,
        VisibilityOn,
        VisibilityOff,
        Lock,
        Unlock,
        Expanded,
        Collapsed,
        Count
    };

    namespace EditorIcons
    {
        /// Loads the icon textures from EnginePaths::Assets()/Editor/Icons.
        /// Safe to call more than once; the first successful call wins.
        void Initialize();

        void Shutdown();

        [[nodiscard]] bool IsInitialized();

        [[nodiscard]] ImTextureID GetTextureID(EditorIcon icon);

        /// Draws an inline icon at the current cursor. The glyph is white, so the
        /// tint defaults to the current ImGui text color. Callers that inline the
        /// icon should call ImGui::SameLine() afterwards.
        void Draw(EditorIcon icon, float size = 0.0f, const ImVec4* tint = nullptr);

        bool ImageButton(const char* id, EditorIcon icon, float size, bool active = false);

        bool Selectable(EditorIcon icon, const char* label, bool selected,
                        ImGuiSelectableFlags flags = 0);

        bool MenuItem(EditorIcon icon, const char* label, const char* shortcut = nullptr,
                      bool selected = false, bool enabled = true);

        [[nodiscard]] EditorIcon ForAssetType(AssetType type);
    }
}
