#include "SceneSerializer.hpp"

#include "Assets/AssetManager.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scene/Components/AudioComponents.hpp"
#include "Scene/Components/ParticleComponents.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Entity.hpp"
#include "Scripting/ScriptClass.hpp"
#include "Scripting/ScriptEngine.hpp"
#include "Scripting/ScriptUtils.hpp"
#include "Serialization/ScriptFieldSerializer.hpp"
#include "SerializationUtils.hpp"
#include "Profiling/Profilers.hpp"

#include <glm/glm.hpp>

#include <fstream>
#include <yaml-cpp/yaml.h>

import Kerberos;

namespace Kerberos {

static void SerializeEntity(YAML::Emitter& out, const Entity entity)
{
    KBR_TRACY_FUNCTION();

    out << YAML::BeginMap;

    out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

    if (entity.HasComponent<TagComponent>()) {
        out << YAML::Key << "TagComponent";
        out << YAML::BeginMap;
        const auto& tag = entity.GetComponent<TagComponent>().Tag;
        out << YAML::Key << "Tag" << YAML::Value << tag;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<TransformComponent>()) {
        out << YAML::Key << "TransformComponent";
        out << YAML::BeginMap;

        const auto& transform = entity.GetComponent<TransformComponent>();
        out << YAML::Key << "Translation" << YAML::Value << transform.Translation;
        out << YAML::Key << "Rotation" << YAML::Value << transform.Rotation;
        out << YAML::Key << "Scale" << YAML::Value << transform.Scale;

        out << YAML::EndMap;
    }

    if (entity.HasComponent<SpriteRendererComponent>()) {
        out << YAML::Key << "SpriteRendererComponent";
        out << YAML::BeginMap;

        const auto& color = entity.GetComponent<SpriteRendererComponent>().Color;
        out << YAML::Key << "Color" << YAML::Value << color;

        out << YAML::EndMap;
    }

    if (entity.HasComponent<CameraComponent>()) {
        out << YAML::Key << "CameraComponent";
        out << YAML::BeginMap;

        const auto& cameraComponent = entity.GetComponent<CameraComponent>();
        const auto& camera = cameraComponent.Camera;

        out << YAML::Key << "Camera" << YAML::Value;
        out << YAML::BeginMap;

        out << YAML::Key << "ProjectionType" << YAML::Value << static_cast<int>(camera.GetProjectionType());
        out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveFov();
        out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
        out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
        out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
        out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
        out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
        out << YAML::EndMap;

        out << YAML::Key << "IsPrimary" << YAML::Value << cameraComponent.IsPrimary;
        out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;

        out << YAML::EndMap;
    }

    if (entity.HasComponent<HierarchyComponent>()) {
        out << YAML::Key << "HierarchyComponent";
        out << YAML::BeginMap;
        const auto& hierarchy = entity.GetComponent<HierarchyComponent>();
        out << YAML::Key << "Parent" << YAML::Value << hierarchy.Parent;
        out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
        for (const auto& child : hierarchy.Children) {
            out << child;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<ScriptComponent>()) {
        out << YAML::Key << "ScriptComponent";
        out << YAML::BeginMap;
        const auto& script = entity.GetComponent<ScriptComponent>();
        out << YAML::Key << "Scripts" << YAML::Value << YAML::BeginSeq;

        for (const auto& className : script.ClassNames) {
            out << YAML::BeginMap;
            out << YAML::Key << "ClassName" << YAML::Value << className;
            out << YAML::Key << "ScriptFields" << YAML::Value << YAML::BeginSeq;

            const auto& fieldInitializers = ScriptEngine::GetScriptFieldInitializerMap(entity, className);

            if (!fieldInitializers.empty()) {
                for (const auto& [name, field] : fieldInitializers) {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Name" << YAML::Value << name;
                    out << YAML::Key << "Type" << YAML::Value << ScriptUtils::ScriptFieldTypeToString(field.Field.Type);
                    ScriptFieldSerializer::Serialize(out, field);
                    out << YAML::EndMap;
                }
            }

            out << YAML::EndSeq;
            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<NativeScriptComponent>()) {
    }

    if (entity.HasComponent<DirectionalLightComponent>()) {
        out << YAML::Key << "DirectionalLightComponent";
        out << YAML::BeginMap;
        const auto& directionalLight = entity.GetComponent<DirectionalLightComponent>();
        out << YAML::Key << "Color" << YAML::Value << directionalLight.Light.Color;
        out << YAML::Key << "Direction" << YAML::Value << directionalLight.Light.Direction;
        out << YAML::Key << "Intensity" << YAML::Value << directionalLight.Light.Intensity;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<PointLightComponent>()) {
        out << YAML::Key << "PointLightComponent";
        out << YAML::BeginMap;
        const auto& pointLight = entity.GetComponent<PointLightComponent>();
        out << YAML::Key << "Color" << YAML::Value << pointLight.Light.Color;
        out << YAML::Key << "Intensity" << YAML::Value << pointLight.Light.Intensity;
        out << YAML::Key << "Radius" << YAML::Value << pointLight.Light.Radius;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<SpotLightComponent>()) {
        out << YAML::Key << "SpotLightComponent";
        out << YAML::BeginMap;
        const auto& spotLight = entity.GetComponent<SpotLightComponent>();
        out << YAML::Key << "Color" << YAML::Value << spotLight.Light.Color;
        out << YAML::Key << "Direction" << YAML::Value << spotLight.Light.Direction;
        out << YAML::Key << "Intensity" << YAML::Value << spotLight.Light.Intensity;
        out << YAML::Key << "Radius" << YAML::Value << spotLight.Light.Radius;
        out << YAML::Key << "CutOffAngleRadians" << YAML::Value << spotLight.Light.CutOffAngleRadians;
        out << YAML::Key << "OuterCutOffAngleRadians" << YAML::Value << spotLight.Light.OuterCutOffAngleRadians;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<RigidBody3DComponent>()) {
        out << YAML::Key << "RigidBody3DComponent";
        out << YAML::BeginMap;
        const auto& rigidBody = entity.GetComponent<RigidBody3DComponent>();
        out << YAML::Key << "Mass" << YAML::Value << rigidBody.Mass;
        out << YAML::Key << "Type" << YAML::Value << static_cast<int>(rigidBody.Type);
        out << YAML::Key << "Velocity" << YAML::Value << rigidBody.Velocity;
        out << YAML::Key << "AngularVelocity" << YAML::Value << rigidBody.AngularVelocity;
        out << YAML::Key << "UseGravity" << YAML::Value << rigidBody.UseGravity;
        out << YAML::Key << "Friction" << YAML::Value << rigidBody.Friction;
        out << YAML::Key << "Restitution" << YAML::Value << rigidBody.Restitution;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<BoxCollider3DComponent>()) {
        out << YAML::Key << "BoxCollider3DComponent";
        out << YAML::BeginMap;
        const auto& boxCollider = entity.GetComponent<BoxCollider3DComponent>();
        out << YAML::Key << "Size" << YAML::Value << boxCollider.Size;
        out << YAML::Key << "Offset" << YAML::Value << boxCollider.Offset;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<SphereCollider3DComponent>()) {
        out << YAML::Key << "SphereCollider3DComponent";
        out << YAML::BeginMap;
        const auto& sphereCollider = entity.GetComponent<SphereCollider3DComponent>();
        out << YAML::Key << "Radius" << YAML::Value << sphereCollider.Radius;
        out << YAML::Key << "Offset" << YAML::Value << sphereCollider.Offset;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<CapsuleCollider3DComponent>()) {
        out << YAML::Key << "CapsuleCollider3DComponent";
        out << YAML::BeginMap;
        const auto& capsuleCollider = entity.GetComponent<CapsuleCollider3DComponent>();
        out << YAML::Key << "Radius" << YAML::Value << capsuleCollider.Radius;
        out << YAML::Key << "Height" << YAML::Value << capsuleCollider.Height;
        out << YAML::Key << "Offset" << YAML::Value << capsuleCollider.Offset;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<MeshCollider3DComponent>()) {
        out << YAML::Key << "MeshCollider3DComponent";
        out << YAML::BeginMap;
        const auto& meshCollider = entity.GetComponent<MeshCollider3DComponent>();
        out << YAML::Key << "Mesh" << YAML::Value
            << (meshCollider.Mesh ? meshCollider.Mesh->GetHandle() : UUID::Invalid());
        out << YAML::Key << "IsTrigger" << YAML::Value << meshCollider.IsTrigger;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<StaticMeshComponent>()) {
        out << YAML::Key << "StaticMeshComponent";
        out << YAML::BeginMap;
        const auto& staticMesh = entity.GetComponent<StaticMeshComponent>();
        const AssetHandle meshHandle = staticMesh.StaticMesh ? staticMesh.StaticMesh->GetHandle() : UUID::Invalid();
        out << YAML::Key << "Mesh" << YAML::Value << meshHandle;
        const AssetHandle materialHandle =
            staticMesh.MeshMaterial ? staticMesh.MeshMaterial->GetHandle() : UUID::Invalid();
        out << YAML::Key << "Material" << YAML::Value << materialHandle;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<ModelComponent>()) {
        out << YAML::Key << "ModelComponent";
        out << YAML::BeginMap;
        const auto& modelComponent = entity.GetComponent<ModelComponent>();
        out << YAML::Key << "ModelAsset" << YAML::Value << modelComponent.ModelAsset;
        out << YAML::Key << "Visible" << YAML::Value << modelComponent.Visible;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<EnvironmentComponent>()) {
        out << YAML::Key << "EnvironmentComponent";
        out << YAML::BeginMap;
        const auto& environmentComponent = entity.GetComponent<EnvironmentComponent>();
        out << YAML::Key << "SkyboxTexture" << YAML::Value << environmentComponent.SkyboxTexture;
        out << YAML::Key << "IsSkyboxEnabled" << YAML::Value << environmentComponent.IsSkyboxEnabled;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<TextComponent>()) {
        out << YAML::Key << "TextComponent";
        out << YAML::BeginMap;
        const auto& textComponent = entity.GetComponent<TextComponent>();
        out << YAML::Key << "Text" << YAML::Value << textComponent.Text;
        out << YAML::Key << "FontSize" << YAML::Value << textComponent.FontSize;
        out << YAML::Key << "Color" << YAML::Value << textComponent.Color;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<AudioSource3DComponent>()) {
        out << YAML::Key << "AudioSource3DComponent";
        out << YAML::BeginMap;
        const auto& audioSource = entity.GetComponent<AudioSource3DComponent>();
        out << YAML::Key << "SoundAsset" << YAML::Value
            << (audioSource.SoundAsset ? audioSource.SoundAsset->GetHandle() : UUID::Invalid());
        out << YAML::Key << "Loop" << YAML::Value << audioSource.Loop;
        out << YAML::Key << "Volume" << YAML::Value << audioSource.Volume;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<AudioSource2DComponent>()) {
        out << YAML::Key << "AudioSource2DComponent";
        out << YAML::BeginMap;
        const auto& audioSource = entity.GetComponent<AudioSource2DComponent>();
        out << YAML::Key << "SoundAsset" << YAML::Value
            << (audioSource.SoundAsset ? audioSource.SoundAsset->GetHandle() : UUID::Invalid());
        out << YAML::Key << "Loop" << YAML::Value << audioSource.Loop;
        out << YAML::Key << "Volume" << YAML::Value << audioSource.Volume;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<AudioListenerComponent>()) {
        out << YAML::Key << "AudioListenerComponent";
        out << YAML::BeginMap;
        const auto& audioListener = entity.GetComponent<AudioListenerComponent>();
        out << YAML::Key << "Volume" << YAML::Value << audioListener.Volume;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<ParticleEmitterComponent>()) {
        out << YAML::Key << "ParticleEmitterComponent";
        out << YAML::BeginMap;
        const auto& particleEmitter = entity.GetComponent<ParticleEmitterComponent>();
        out << YAML::Key << "IsActive" << YAML::Value << particleEmitter.IsActive;
        out << YAML::Key << "SpawnRate" << YAML::Value << particleEmitter.SpawnRate;
        out << YAML::Key << "MinLifetime" << YAML::Value << particleEmitter.MinLifetime;
        out << YAML::Key << "MaxLifetime" << YAML::Value << particleEmitter.MaxLifetime;
        out << YAML::Key << "MinVelocity" << YAML::Value << particleEmitter.MinVelocity;
        out << YAML::Key << "MaxVelocity" << YAML::Value << particleEmitter.MaxVelocity;
        out << YAML::Key << "MinAcceleration" << YAML::Value << particleEmitter.MinAcceleration;
        out << YAML::Key << "MaxAcceleration" << YAML::Value << particleEmitter.MaxAcceleration;
        out << YAML::Key << "StartColor" << YAML::Value << particleEmitter.StartColor;
        out << YAML::Key << "EndColor" << YAML::Value << particleEmitter.EndColor;
        out << YAML::Key << "StartSize" << YAML::Value << particleEmitter.StartSize;
        out << YAML::Key << "EndSize" << YAML::Value << particleEmitter.EndSize;
        out << YAML::Key << "SubUVGrid" << YAML::Value << particleEmitter.SubUVGrid;
        out << YAML::Key << "ParticleTexture" << YAML::Value << particleEmitter.ParticleTexture;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<PrefabInstanceComponent>()) {
        out << YAML::Key << "PrefabInstanceComponent";
        out << YAML::BeginMap;
        const auto& prefabInstance = entity.GetComponent<PrefabInstanceComponent>();
        out << YAML::Key << "PrefabAsset" << YAML::Value << prefabInstance.PrefabAsset;
        out << YAML::Key << "IsExpandedInHierarchy" << YAML::Value << prefabInstance.IsExpandedInHierarchy;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<SkeletalMeshComponent>()) {
        out << YAML::Key << "SkeletalMeshComponent";
        out << YAML::BeginMap;
        const auto& skeletalMesh = entity.GetComponent<SkeletalMeshComponent>();
        out << YAML::Key << "MeshAsset" << YAML::Value << skeletalMesh.MeshAsset;
        out << YAML::Key << "SkeletonAsset" << YAML::Value << skeletalMesh.SkeletonAsset;
        out << YAML::Key << "MaterialAsset" << YAML::Value << skeletalMesh.MaterialAsset;
        out << YAML::Key << "Visible" << YAML::Value << skeletalMesh.Visible;
        out << YAML::Key << "CastShadows" << YAML::Value << skeletalMesh.CastShadows;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<SkinComponent>()) {
        out << YAML::Key << "SkinComponent";
        out << YAML::BeginMap;
        const auto& skin = entity.GetComponent<SkinComponent>();
        out << YAML::Key << "SkeletonAsset" << YAML::Value << skin.SkeletonAsset;
        out << YAML::Key << "JointEntities" << YAML::Value << YAML::BeginSeq;
        for (const auto& jointEntity : skin.JointEntities) {
            out << jointEntity;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
    }

    if (entity.HasComponent<AnimationComponent>()) {
        out << YAML::Key << "AnimationComponent";
        out << YAML::BeginMap;
        const auto& animation = entity.GetComponent<AnimationComponent>();
        out << YAML::Key << "AnimationAsset" << YAML::Value << animation.AnimationAsset;
        out << YAML::Key << "PlaybackSpeed" << YAML::Value << animation.PlaybackSpeed;
        out << YAML::Key << "IsPlaying" << YAML::Value << animation.IsPlaying;
        out << YAML::Key << "IsLooping" << YAML::Value << animation.IsLooping;
        out << YAML::EndMap;
    }

    out << YAML::EndMap;
}

void SceneSerializer::Serialize(const std::filesystem::path& filepath) const
{
    KBR_TRACY_FUNCTION();

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Scene" << YAML::Value << m_Scene->GetName();
    out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
    for (const auto entityId : m_Scene->m_Registry.view<entt::entity>()) {
        const Entity entity{ entityId, m_Scene.get() };
        if (!entity)
            continue;

        SerializeEntity(out, entity);
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    /// Check if there's a parent directory
    if (const std::filesystem::path parentDir = filepath.parent_path(); !parentDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parentDir, ec);
        if (ec) {
            Log::CoreError("Could not create directory {0}: {1}", parentDir.string(), ec.message());
        }
    }

    if (std::ofstream fileOut(filepath); fileOut) {
        fileOut << out.c_str();
        fileOut.close();
    }
    else {
        Log::CoreError("Could not open file {0} for writing!", filepath.string());
    }
}

void SceneSerializer::SerializeRuntime([[maybe_unused]] const std::filesystem::path& filepath)
{
    throw std::logic_error("Not implemented");
}

static void DeserializeScriptFieldInitializers(const YAML::Node& scriptFields, const Entity entity, const std::string& className)
{
    auto& scriptFieldInitializers = ScriptEngine::GetScriptFieldInitializerMap(entity, className);

    for (const auto& field : scriptFields) {
        const std::string name = field["Name"].as<std::string>();
        const ScriptFieldType type =
            ScriptUtils::StringToScriptFieldType(field["Type"].as<std::string>());
        ScriptFieldInitializer initializer;
        initializer.Field.Name = name;
        initializer.Field.Type = type;
        ScriptFieldSerializer::Deserialize(field, initializer);

        scriptFieldInitializers[name] = initializer;
    }

    /// Reconcile the serialized values with the current script class definition
    /// so fields removed from the script stop showing up (and get dropped on the
    /// next save) and fields added to the script get defaults immediately,
    /// before the inspector is ever drawn.
    if (ScriptEngine::ClassExists(className))
        ScriptEngine::CreateScriptFieldInitializers(entity, className);
}

Entity SceneSerializer::DeserializeEntity(const Ref<Scene>& scene, const YAML::Node& node)
{
    uint64_t uuid = node["Entity"].as<uint64_t>();
    std::string tag;
    if (node["TagComponent"])
        tag = node["TagComponent"]["Tag"].as<std::string>();

    /// Create the entity with the given tag and id
    Entity deserializedEntity = scene->CreateEntityWithUUID(tag, uuid);

    if (auto transformComponent = node["TransformComponent"]) {
        auto& transform = deserializedEntity.GetComponent<TransformComponent>();
        transform.Translation = transformComponent["Translation"].as<glm::vec3>();
        transform.Rotation = transformComponent["Rotation"].as<glm::vec3>();
        transform.Scale = transformComponent["Scale"].as<glm::vec3>();
    }

    if (auto cameraComponent = node["CameraComponent"]) {
        auto& comp = deserializedEntity.AddComponent<CameraComponent>();
        auto cameraData = cameraComponent["Camera"];

        comp.Camera.SetProjectionType(
            static_cast<SceneCamera::ProjectionType>(cameraData["ProjectionType"].as<int>()));

        comp.Camera.SetPerspectiveFov(cameraData["PerspectiveFOV"].as<float>());
        comp.Camera.SetPerspectiveNearClip(cameraData["PerspectiveNear"].as<float>());
        comp.Camera.SetPerspectiveFarClip(cameraData["PerspectiveFar"].as<float>());

        comp.Camera.SetOrthographicSize(cameraData["OrthographicSize"].as<float>());
        comp.Camera.SetOrthographicNearClip(cameraData["OrthographicNear"].as<float>());
        comp.Camera.SetOrthographicFarClip(cameraData["OrthographicFar"].as<float>());

        comp.IsPrimary = cameraComponent["IsPrimary"].as<bool>();
        comp.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
    }

    if (auto spriteRendererComponent = node["SpriteRendererComponent"]) {
        auto& spriteRenderer = deserializedEntity.AddComponent<SpriteRendererComponent>();
        spriteRenderer.Color = spriteRendererComponent["Color"].as<glm::vec4>();
    }

    if (auto hierarchyComponent = node["HierarchyComponent"]) {
        /// The entity must have a HierarchyComponent already when created
        auto& hierarchy = deserializedEntity.GetComponent<HierarchyComponent>();
        const UUID parentId = static_cast<UUID>(hierarchyComponent["Parent"].as<uint64_t>());

        hierarchy.Parent = parentId;
        if (parentId.IsValid()) {
            /// If the entity has a parent, it should not be a root entity
            auto& rootEntities = scene->m_RootEntities;
            auto it = std::ranges::find(rootEntities, static_cast<entt::entity>(deserializedEntity));
            if (it != rootEntities.end())
                rootEntities.erase(it);
        }

        for (const auto& child : hierarchyComponent["Children"]) {
            const uint64_t childUUID = child.as<uint64_t>();
            hierarchy.Children.emplace_back(childUUID);
        }
    }

    if (auto scriptComponent = node["ScriptComponent"]) {
        auto& script = deserializedEntity.AddComponent<ScriptComponent>();

        if (auto scripts = scriptComponent["Scripts"]) {
            for (const auto& scriptNode : scripts) {
                const std::string className =
                    ScriptUtils::MigrateLegacyScriptClassName(scriptNode["ClassName"].as<std::string>());
                script.ClassNames.push_back(className);
                if (auto scriptFields = scriptNode["ScriptFields"]) {
                    DeserializeScriptFieldInitializers(scriptFields, deserializedEntity, className);
                }
            }
        }
        else if (auto className = scriptComponent["ClassName"]) {
            /// Legacy single-script format.
            const std::string legacyClassName =
                ScriptUtils::MigrateLegacyScriptClassName(className.as<std::string>());
            script.ClassNames.push_back(legacyClassName);
            if (auto scriptFields = scriptComponent["ScriptFields"]) {
                DeserializeScriptFieldInitializers(scriptFields, deserializedEntity, legacyClassName);
            }
        }
    }

    if (auto nativeScriptComponent = node["NativeScriptComponent"]) {
        // Handle NativeScriptComponent deserialization here
        // This is a placeholder as the actual implementation depends on the scripting system used
    }

    if (auto directionalLightComponent = node["DirectionalLightComponent"]) {
        auto& directionalLight = deserializedEntity.AddComponent<DirectionalLightComponent>();
        directionalLight.Light.Color = directionalLightComponent["Color"].as<glm::vec3>();
        directionalLight.Light.Direction = directionalLightComponent["Direction"].as<glm::vec3>();
        directionalLight.Light.Intensity = directionalLightComponent["Intensity"].as<float>();
    }

    if (auto pointLightComponent = node["PointLightComponent"]) {
        auto& pointLight = deserializedEntity.AddComponent<PointLightComponent>();
        pointLight.Light.Color = pointLightComponent["Color"].as<glm::vec3>();
        pointLight.Light.Intensity = pointLightComponent["Intensity"].as<float>();
        pointLight.Light.Radius = pointLightComponent["Radius"].as<float>();
    }

    if (auto spotLightComponent = node["SpotLightComponent"]) {
        auto& spotLight = deserializedEntity.AddComponent<SpotLightComponent>();
        spotLight.Light.Color = spotLightComponent["Color"].as<glm::vec3>();
        spotLight.Light.Direction = spotLightComponent["Direction"].as<glm::vec3>();
        spotLight.Light.Intensity = spotLightComponent["Intensity"].as<float>();
        spotLight.Light.Radius = spotLightComponent["Radius"].as<float>();
        spotLight.Light.CutOffAngleRadians = spotLightComponent["CutOffAngleRadians"].as<float>();
        spotLight.Light.OuterCutOffAngleRadians = spotLightComponent["OuterCutOffAngleRadians"].as<float>();
    }

    if (auto rigidBodyComponent = node["RigidBody3DComponent"]) {
        auto& rigidBody = deserializedEntity.AddComponent<RigidBody3DComponent>();
        rigidBody.Mass = rigidBodyComponent["Mass"].as<float>();
        rigidBody.Type = static_cast<RigidBody3DComponent::BodyType>(rigidBodyComponent["Type"].as<int>());
        rigidBody.Velocity = rigidBodyComponent["Velocity"].as<glm::vec3>();
        rigidBody.AngularVelocity = rigidBodyComponent["AngularVelocity"].as<glm::vec3>();
        rigidBody.UseGravity = rigidBodyComponent["UseGravity"].as<bool>();
        rigidBody.Friction = rigidBodyComponent["Friction"].as<float>();
        rigidBody.Restitution = rigidBodyComponent["Restitution"].as<float>();
    }

    if (auto boxColliderComponent = node["BoxCollider3DComponent"]) {
        auto& boxCollider = deserializedEntity.AddComponent<BoxCollider3DComponent>();
        boxCollider.Size = boxColliderComponent["Size"].as<glm::vec3>();
        boxCollider.Offset = boxColliderComponent["Offset"].as<glm::vec3>();
    }

    if (auto sphereColliderComponent = node["SphereCollider3DComponent"]) {
        auto& sphereCollider = deserializedEntity.AddComponent<SphereCollider3DComponent>();
        sphereCollider.Radius = sphereColliderComponent["Radius"].as<float>();
        sphereCollider.Offset = sphereColliderComponent["Offset"].as<glm::vec3>();
    }

    if (auto capsuleColliderComponent = node["CapsuleCollider3DComponent"]) {
        auto& capsuleCollider = deserializedEntity.AddComponent<CapsuleCollider3DComponent>();
        capsuleCollider.Radius = capsuleColliderComponent["Radius"].as<float>();
        capsuleCollider.Height = capsuleColliderComponent["Height"].as<float>();
        capsuleCollider.Offset = capsuleColliderComponent["Offset"].as<glm::vec3>();
    }

    if (auto meshColliderComponent = node["MeshCollider3DComponent"]) {
        auto& meshCollider = deserializedEntity.AddComponent<MeshCollider3DComponent>();
        meshCollider.IsTrigger = meshColliderComponent["IsTrigger"].as<bool>();
        const AssetHandle meshHandle = AssetHandle(meshColliderComponent["Mesh"].as<uint64_t>());
        if (meshHandle.IsValid()) {
            meshCollider.Mesh = AssetManager::GetAsset<Mesh>(meshHandle);
        }
    }

    if (auto staticMeshComponent = node["StaticMeshComponent"]) {
        auto& staticMesh = deserializedEntity.AddComponent<StaticMeshComponent>();

        const AssetHandle materialHandle = AssetHandle(staticMeshComponent["Material"].as<uint64_t>());
        if (materialHandle.IsValid())
            staticMesh.MeshMaterial = AssetManager::GetAsset<Material>(materialHandle);

        const AssetHandle meshHandle = AssetHandle(staticMeshComponent["Mesh"].as<uint64_t>());
        if (meshHandle.IsValid()) {
            staticMesh.StaticMesh = AssetManager::GetAsset<Mesh>(meshHandle);
        }
        else {
            Log::CoreWarn("AssetHandle for mesh is invalid, using default cube mesh.");
            staticMesh.StaticMesh = AssetManager::GetDefaultCubeMesh();
        }
    }

    if (auto modelComponent = node["ModelComponent"]) {
        auto& model = deserializedEntity.AddComponent<ModelComponent>();
        model.ModelAsset = AssetHandle(modelComponent["ModelAsset"].as<uint64_t>());
        model.Visible = modelComponent["Visible"].as<bool>(true);
    }

    if (auto environmentComponent = node["EnvironmentComponent"]) {
        auto& environment = deserializedEntity.AddComponent<EnvironmentComponent>();
        environment.SkyboxTexture = AssetHandle(environmentComponent["SkyboxTexture"].as<uint64_t>());
        environment.IsSkyboxEnabled = environmentComponent["IsSkyboxEnabled"].as<bool>();
    }

    if (auto textComponent = node["TextComponent"]) {
        auto& text = deserializedEntity.AddComponent<TextComponent>();
        text.Text = textComponent["Text"].as<std::string>();
        text.Color = textComponent["Color"].as<glm::vec4>();
        text.FontSize = textComponent["FontSize"].as<float>();
    }

    if (auto audioSource3DComponent = node["AudioSource3DComponent"]) {
        auto& audioSource3D = deserializedEntity.AddComponent<AudioSource3DComponent>();
        audioSource3D.SoundAsset =
            AssetManager::GetAsset<Sound>(AssetHandle(audioSource3DComponent["SoundAsset"].as<uint64_t>()));
        audioSource3D.Loop = audioSource3DComponent["Loop"].as<bool>();
        audioSource3D.Volume = audioSource3DComponent["Volume"].as<float>();
    }

    if (auto audioSource2DComponent = node["AudioSource2DComponent"]) {
        auto& audioSource2D = deserializedEntity.AddComponent<AudioSource2DComponent>();
        audioSource2D.SoundAsset =
            AssetManager::GetAsset<Sound>(AssetHandle(audioSource2DComponent["SoundAsset"].as<uint64_t>()));
        audioSource2D.Loop = audioSource2DComponent["Loop"].as<bool>();
        audioSource2D.Volume = audioSource2DComponent["Volume"].as<float>();
    }

    if (auto audioListenerComponent = node["AudioListenerComponent"]) {
        auto& audioListener = deserializedEntity.AddComponent<AudioListenerComponent>();
        audioListener.Volume = audioListenerComponent["Volume"].as<float>();
    }

    if (auto particleEmitterComponent = node["ParticleEmitterComponent"]) {
        auto& particleEmitter = deserializedEntity.AddComponent<ParticleEmitterComponent>();
        particleEmitter.IsActive = particleEmitterComponent["IsActive"].as<bool>();
        particleEmitter.SpawnRate = particleEmitterComponent["SpawnRate"].as<float>();
        particleEmitter.MinLifetime = particleEmitterComponent["MinLifetime"].as<float>();
        particleEmitter.MaxLifetime = particleEmitterComponent["MaxLifetime"].as<float>();
        particleEmitter.MinVelocity = particleEmitterComponent["MinVelocity"].as<glm::vec3>();
        particleEmitter.MaxVelocity = particleEmitterComponent["MaxVelocity"].as<glm::vec3>();
        particleEmitter.MinAcceleration = particleEmitterComponent["MinAcceleration"].as<glm::vec3>();
        particleEmitter.MaxAcceleration = particleEmitterComponent["MaxAcceleration"].as<glm::vec3>();
        particleEmitter.StartColor = particleEmitterComponent["StartColor"].as<glm::vec4>();
        particleEmitter.EndColor = particleEmitterComponent["EndColor"].as<glm::vec4>();
        particleEmitter.StartSize = particleEmitterComponent["StartSize"].as<float>();
        particleEmitter.EndSize = particleEmitterComponent["EndSize"].as<float>();
        particleEmitter.SubUVGrid = particleEmitterComponent["SubUVGrid"].as<glm::vec2>();
        particleEmitter.ParticleTexture =
            AssetHandle(particleEmitterComponent["ParticleTexture"].as<uint64_t>());
    }

    if (auto prefabInstanceComponent = node["PrefabInstanceComponent"]) {
        auto& prefabInstance = deserializedEntity.AddComponent<PrefabInstanceComponent>();
        prefabInstance.PrefabAsset = AssetHandle(prefabInstanceComponent["PrefabAsset"].as<uint64_t>());
        prefabInstance.IsExpandedInHierarchy =
            prefabInstanceComponent["IsExpandedInHierarchy"].as<bool>(true);
    }

    if (auto skeletalMeshComponent = node["SkeletalMeshComponent"]) {
        auto& skeletalMesh = deserializedEntity.AddComponent<SkeletalMeshComponent>();
        skeletalMesh.MeshAsset = AssetHandle(skeletalMeshComponent["MeshAsset"].as<uint64_t>());
        skeletalMesh.SkeletonAsset = AssetHandle(skeletalMeshComponent["SkeletonAsset"].as<uint64_t>());
        skeletalMesh.MaterialAsset = AssetHandle(skeletalMeshComponent["MaterialAsset"].as<uint64_t>());
        skeletalMesh.Visible = skeletalMeshComponent["Visible"].as<bool>(true);
        skeletalMesh.CastShadows = skeletalMeshComponent["CastShadows"].as<bool>(true);
    }

    if (auto skinComponent = node["SkinComponent"]) {
        auto& skin = deserializedEntity.AddComponent<SkinComponent>();
        skin.SkeletonAsset = AssetHandle(skinComponent["SkeletonAsset"].as<uint64_t>());
        if (auto jointEntities = skinComponent["JointEntities"]) {
            for (const auto& jointEntity : jointEntities) {
                skin.JointEntities.emplace_back(jointEntity.as<uint64_t>());
            }
        }
    }

    if (auto animationComponent = node["AnimationComponent"]) {
        auto& animation = deserializedEntity.AddComponent<AnimationComponent>();
        animation.AnimationAsset = AssetHandle(animationComponent["AnimationAsset"].as<uint64_t>());
        animation.PlaybackSpeed = animationComponent["PlaybackSpeed"].as<float>(1.0f);
        animation.IsPlaying = animationComponent["IsPlaying"].as<bool>(false);
        animation.IsLooping = animationComponent["IsLooping"].as<bool>(true);
    }

    return deserializedEntity;
}

bool SceneSerializer::Deserialize(const std::filesystem::path& filepath) const
{
    KBR_TRACY_FUNCTION();

    const std::ifstream inFile(filepath);
    std::stringstream stream;
    stream << inFile.rdbuf();

    YAML::Node data = YAML::Load(stream.str());
    if (!data["Scene"]) {
        Log::CoreError("Invalid Scene file {0}", filepath.string());
        return false;
    }

    const std::string sceneName = data["Scene"].as<std::string>();
    m_Scene->SetName(sceneName);

    if (auto entities = data["Entities"]) {
        for (const auto& entity : entities) {
            DeserializeEntity(m_Scene, entity);
        }
    }

    return true;
}

bool SceneSerializer::DeserializeRuntime([[maybe_unused]] const std::filesystem::path& filepath) const
{
    throw std::logic_error("Not implemented");
}

static void SerializeEntitySubtreeRecursive(YAML::Emitter& out, const Ref<Scene>& scene, const Entity& entity)
{
    SerializeEntity(out, entity);

    if (!entity.HasComponent<HierarchyComponent>())
        return;

    for (const UUID& childID : entity.GetComponent<HierarchyComponent>().Children) {
        if (const Entity child = scene->TryGetEntityByUUID(childID))
            SerializeEntitySubtreeRecursive(out, scene, child);
    }
}

std::string SceneSerializer::SerializeEntitySubtree(const Ref<Scene>& scene, const Entity& entity)
{
    if (!scene || !entity)
        return {};

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "Root" << YAML::Value << entity.GetUUID();
    out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
    SerializeEntitySubtreeRecursive(out, scene, entity);
    out << YAML::EndSeq;
    out << YAML::EndMap;

    return out.c_str();
}

Entity SceneSerializer::DeserializeEntitySubtree(const Ref<Scene>& scene, const std::string& data)
{
    if (!scene || data.empty())
        return {};

    const YAML::Node root = YAML::Load(data);
    if (const auto entities = root["Entities"]) {
        for (const auto& entity : entities) {
            DeserializeEntity(scene, entity);
        }
    }

    if (const auto rootID = root["Root"]) {
        return scene->TryGetEntityByUUID(UUID(rootID.as<uint64_t>()));
    }

    return {};
}

}