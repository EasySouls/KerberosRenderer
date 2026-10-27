#include "PrefabAsset.hpp"

#include "Scene/Entity.hpp"
#include "Scene/Components/AnimationComponents.hpp"
#include "Scripting/ScriptEngine.hpp"

namespace Kerberos {

void PrefabEntityTemplate::ToEntity(Entity newEntity) const
{
    auto& transform = newEntity.GetComponent<TransformComponent>();
    transform.Translation = Transform.Translation;
    transform.Rotation = Transform.Rotation;
    transform.Scale = Transform.Scale;
    transform.WorldTransform = glm::mat4(1.0f);
    if (SkinnedMesh)
    {
        auto& c = newEntity.AddComponent<SkinnedMeshComponent>();
        c.MeshAsset = SkinnedMesh->MeshAsset;
        c.SkeletonAsset = SkinnedMesh->SkeletonAsset;
        c.MaterialAsset = SkinnedMesh->MaterialAsset;
        c.Visible = SkinnedMesh->Visible;
        c.CastShadows = SkinnedMesh->CastShadows;
    }
    if (Animation)
    {
        auto& c = newEntity.AddComponent<AnimationComponent>();
        c.AnimationAsset = Animation->AnimationAsset;
        c.PlaybackSpeed = Animation->PlaybackSpeed;
        c.IsLooping = Animation->Loop;
        c.IsPlaying = Animation->AutoPlay;
        c.CurrentTime = 0.0f;
    }
    if (Skin)
    {
        auto& c = newEntity.AddComponent<SkeletonComponent>();
        c.SkeletonAsset = Skin->SkeletonAsset;
    }
    if (HasStaticMesh && StaticMeshAsset.IsValid())
    {
        auto& c = newEntity.AddComponent<StaticMeshComponent>();
        c.StaticMesh = AssetManager::ResolveMeshAsset(StaticMeshAsset);
        c.MeshMaterial = AssetManager::AsAsset<Material>(StaticMaterialAsset);
    }
    if (RigidBody)
    {
        auto& c = newEntity.AddComponent<RigidBody3DComponent>(RigidBody.value());
        c.RuntimeBody = nullptr;
        c.IsDirty = true;
    }
    if (BoxCollider)
    {
        auto& c = newEntity.AddComponent<BoxCollider3DComponent>(BoxCollider.value());
        c.RuntimeCollider = nullptr;
    }
    if (SphereCollider)
    {
        auto& c = newEntity.AddComponent<SphereCollider3DComponent>(SphereCollider.value());
        c.RuntimeCollider = nullptr;
    }
    if (CapsuleCollider)
    {
        auto& c = newEntity.AddComponent<CapsuleCollider3DComponent>(CapsuleCollider.value());
        c.RuntimeCollider = nullptr;
    }
    if (!Scripts.empty())
    {
        auto& scriptComponent = newEntity.AddComponent<ScriptComponent>();
        for (const auto& script : Scripts)
        {
            scriptComponent.ClassNames.push_back(script.ClassName);

            auto& initializers = ScriptEngine::GetScriptFieldInitializerMap(newEntity, script.ClassName);
            for (const auto& fieldInitializer : script.FieldInitializers)
            {
                initializers[fieldInitializer.Field.Name] = fieldInitializer;
            }

            /// Drop fields the script removed and add defaults for new fields.
            if (ScriptEngine::ClassExists(script.ClassName))
                ScriptEngine::CreateScriptFieldInitializers(newEntity, script.ClassName);
        }
    }
}

}