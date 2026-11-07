#include "PrefabSerializer.hpp"
#include "Profiling/Profilers.hpp"
#include "Serialization/ScriptFieldSerializer.hpp"
#include "Scripting/ScriptUtils.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <unordered_map>

import Kerberos;

namespace Kerberos
{
	namespace
	{
		void ReadVec3(const YAML::Node& node, glm::vec3& value)
		{
			if (!node) return;
			const auto v = node.as<std::vector<float>>();
			if (v.size() >= 3) value = glm::vec3(v[0], v[1], v[2]);
		}

		void WriteHandle(YAML::Emitter& out, const char* key, const AssetHandle handle)
		{
			out << YAML::Key << key << YAML::Value << static_cast<uint64_t>(handle);
		}
	}

	bool PrefabSerializer::SerializePrefab(const Ref<Prefab>& prefab, const std::filesystem::path& filepath)
	{
		KBR_TRACY_FUNCTION();

		if (!prefab) 
		{
            return false;
        }

		YAML::Emitter out;
		out << YAML::BeginMap << YAML::Key << "Prefab" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << YAML::DoubleQuoted << prefab->Name;
		out << YAML::Key << "RootLocalIndex" << YAML::Value << prefab->RootLocalIndex;
		out << YAML::EndMap << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		for (size_t i = 0; i < prefab->Entities.size(); ++i)
		{
			const auto& e = prefab->Entities[i];
			const auto index = e.LocalIndex == InvalidPrefabLocalIndex ? static_cast<PrefabLocalIndex>(i) : e.LocalIndex;
			out << YAML::BeginMap << YAML::Key << "LocalIndex" << YAML::Value << index;
            out << YAML::Key << "Name" << YAML::Value << YAML::DoubleQuoted << (e.Name.empty() ? e.Tag : e.Name);
			out << YAML::Key << "ParentLocalIndex" << YAML::Value << e.ParentLocalIndex;
			out << YAML::Key << "Children" << YAML::Value << YAML::Flow << YAML::BeginSeq;

			for (auto child : e.Children) 
			{
				out << child;
			}

			out << YAML::EndSeq;

			auto writeVec = [&out](const char* key, const glm::vec3& v) {
				out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
			};
			writeVec("Translation", e.Transform.Translation);
			writeVec("Rotation", e.Transform.Rotation);
			writeVec("Scale", e.Transform.Scale);
			if (e.JointIndex)
				out << YAML::Key << "JointIndex" << YAML::Value << *e.JointIndex;
			if (e.Skin)
			{
				out << YAML::Key << "Skin" << YAML::Value << YAML::BeginMap;
				WriteHandle(out, "SkeletonAsset", e.Skin->SkeletonAsset);
				out << YAML::Key << "JointEntityIndices" << YAML::Value << YAML::Flow << YAML::BeginSeq;
				for (auto joint : e.Skin->JointEntityIndices) out << joint;
				out << YAML::EndSeq << YAML::EndMap;
			}
			if (e.SkinnedMesh)
			{
				out << YAML::Key << "SkinnedMesh" << YAML::Value << YAML::BeginMap;
				WriteHandle(out, "MeshAsset", e.SkinnedMesh->MeshAsset);
				WriteHandle(out, "SkeletonAsset", e.SkinnedMesh->SkeletonAsset);
				WriteHandle(out, "MaterialAsset", e.SkinnedMesh->MaterialAsset);
				WriteHandle(out, "SkeletonEntity", e.SkinnedMesh->SkeletonEntityID);
				out << YAML::Key << "SkeletonEntityLocalIndex" << YAML::Value << e.SkinnedMesh->SkeletonEntityLocalIndex
					<< YAML::Key << "Visible" << YAML::Value << e.SkinnedMesh->Visible
					<< YAML::Key << "CastShadows" << YAML::Value << e.SkinnedMesh->CastShadows << YAML::EndMap;
			}
			if (e.Animator)
			{
				out << YAML::Key << "Animator" << YAML::Value << YAML::BeginMap;
				WriteHandle(out, "SkeletonEntity", e.Animator->SkeletonEntityID);
				out << YAML::Key << "SkeletonEntityLocalIndex" << YAML::Value << e.Animator->SkeletonEntityLocalIndex;
				out << YAML::Key << "AnimationClips" << YAML::Value << YAML::Flow << YAML::BeginSeq;
				for (const auto& clip : e.Animator->Clips) {
					out << clip;
				}
				out << YAML::EndSeq;
				out << YAML::Key << "ActiveClip" << YAML::Value << e.Animator->ActiveClip;
				out << YAML::Key << "PlaybackSpeed" << YAML::Value << e.Animator->PlaybackSpeed
					<< YAML::Key << "Loop" << YAML::Value << e.Animator->Loop
					<< YAML::Key << "AutoPlay" << YAML::Value << e.Animator->AutoPlay << YAML::EndMap;
			}
			if (e.RigidBody)
			{
				out << YAML::Key << "RigidBody" << YAML::Value << YAML::BeginMap
					<< YAML::Key << "Type" << YAML::Value << static_cast<int>(e.RigidBody->Type)
					<< YAML::Key << "Mass" << YAML::Value << e.RigidBody->Mass
					<< YAML::Key << "UseGravity" << YAML::Value << e.RigidBody->UseGravity
					<< YAML::EndMap;
			}
			if (!e.Scripts.empty())
			{
				out << YAML::Key << "Scripts" << YAML::Value << YAML::BeginSeq;
				for (const auto& script : e.Scripts)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
					out << YAML::Key << "ScriptFields" << YAML::Value << YAML::BeginSeq;
					for (const auto& field : script.FieldInitializers)
					{
						out << YAML::BeginMap;
						out << YAML::Key << "Name" << YAML::Value << field.Field.Name;
						out << YAML::Key << "Type" << YAML::Value
							<< ScriptUtils::ScriptFieldTypeToString(field.Field.Type);
						ScriptFieldSerializer::Serialize(out, field);
						out << YAML::EndMap;
					}
					out << YAML::EndSeq;
					out << YAML::EndMap;
				}
				out << YAML::EndSeq;
			}
			out << YAML::Key << "HasStaticMesh" << YAML::Value << e.HasStaticMesh 
				<< YAML::Key << "MeshAssetPath" << YAML::Value << YAML::DoubleQuoted << e.MeshAssetPath
				<< YAML::Key << "MaterialAssetPath" << YAML::Value << YAML::DoubleQuoted << e.MaterialAssetPath
				<< YAML::EndMap;
		}
		out << YAML::EndSeq << YAML::EndMap;
		std::ofstream file(filepath);
		if (!file) 
		{
			return false;
		}
		file << out.c_str();
		return true;
	}

	Ref<Prefab> PrefabSerializer::DeserializePrefab(const std::filesystem::path& filepath)
	{
		KBR_TRACY_FUNCTION();

		if (!std::filesystem::exists(filepath))
		{
            KBRAssert("Prefab file does not exist: {}", filepath.string());
			return nullptr;
		}

		YAML::Node root;
		try 
		{ 
			root = YAML::LoadFile(filepath.string());
		}
		catch (const YAML::Exception& e) 
		{
			Log::CoreError("Prefab YAML parse failed for file {}: {}", filepath.string(), e.what()); 
			return nullptr;
		}
		if (!root["Prefab"] || !root["Entities"])
		{
			return nullptr;
		}

		auto prefab = CreateRef<Prefab>();
		const auto meta = root["Prefab"];

		prefab->Name = meta["Name"].as<std::string>("Untitled Prefab");
		prefab->RootLocalIndex = meta["RootLocalIndex"].as<PrefabLocalIndex>(InvalidPrefabLocalIndex);
		prefab->RootEntityID = UUID(meta["RootEntityID"].as<uint64_t>(0));

		for (const auto& n : root["Entities"])
		{
			PrefabEntityData e;
			e.LocalIndex = n["LocalIndex"].as<PrefabLocalIndex>(static_cast<PrefabLocalIndex>(prefab->Entities.size()));
			e.ID = UUID(n["ID"].as<uint64_t>(0));
			e.Name = n["Name"].as<std::string>(n["Tag"].as<std::string>("Entity"));
			e.Tag = e.Name;
			ReadVec3(n["Translation"], e.Transform.Translation);
			ReadVec3(n["Rotation"].IsDefined() ? n["Rotation"] : n["EulerRotation"], e.Transform.Rotation);
			ReadVec3(n["Scale"], e.Transform.Scale);
			e.Translation = e.Transform.Translation;
			e.EulerRotation = e.Transform.Rotation;
			if (n["JointIndex"])
				e.JointIndex = n["JointIndex"].as<uint32_t>();
			e.ParentLocalIndex = n["ParentLocalIndex"].as<PrefabLocalIndex>(InvalidPrefabLocalIndex);
			e.Parent = UUID(n["Parent"].as<uint64_t>(0));
            if (n["Children"]) 
			{
				for (const auto& c : n["Children"]) 
				{
                    if (n["ParentLocalIndex"].IsDefined() || n["LocalIndex"].IsDefined())
                        e.Children.push_back(c.as<PrefabLocalIndex>());
                    else
                        e.ChildIDs.emplace_back(c.as<uint64_t>());
                }
			}
			e.HasStaticMesh = n["HasStaticMesh"].as<bool>(false);
			e.MeshAssetPath = n["MeshAssetPath"].as<std::string>("");
			e.MaterialAssetPath = n["MaterialAssetPath"].as<std::string>("");

			if (const YAML::Node& skinNode = n["Skin"]) 
			{
				SkinComponentTemplate s;
				s.SkeletonAsset = AssetHandle(skinNode["SkeletonAsset"].as<uint64_t>(0));
				if (skinNode["JointEntityIndices"])
				{
					for (const auto& j : skinNode["JointEntityIndices"]) 
						s.JointEntityIndices.push_back(j.as<PrefabLocalIndex>());
				}
				e.Skin = s;
			}

			if (const YAML::Node& skinnedMeshNode = n["SkinnedMesh"]) 
			{
				SkinnedMeshComponentTemplate s;
				s.MeshAsset = AssetHandle(skinnedMeshNode["MeshAsset"].as<uint64_t>(0));
				s.SkeletonAsset = AssetHandle(skinnedMeshNode["SkeletonAsset"].as<uint64_t>(0));
				s.MaterialAsset = AssetHandle(skinnedMeshNode["MaterialAsset"].as<uint64_t>(0));
				s.SkeletonEntityID = UUID(skinnedMeshNode["SkeletonEntity"].as<uint64_t>(0));
				s.SkeletonEntityLocalIndex = skinnedMeshNode["SkeletonEntityLocalIndex"].as<PrefabLocalIndex>(InvalidPrefabLocalIndex);
				s.Visible = skinnedMeshNode["Visible"].as<bool>(true);
				s.CastShadows = skinnedMeshNode["CastShadows"].as<bool>(true);
				e.SkinnedMesh = s;
			}

			if (const YAML::Node& animatorNode = n["Animator"])
			{
				AnimatorComponentTemplate a;
				const YAML::Node skeletonEntityNode =
					animatorNode["SkeletonEntity"].IsDefined() ? animatorNode["SkeletonEntity"] : animatorNode["SkeletonAsset"];
				a.SkeletonEntityID = UUID(skeletonEntityNode.as<uint64_t>(UUID::Invalid()));
				a.SkeletonEntityLocalIndex = animatorNode["SkeletonEntityLocalIndex"].as<PrefabLocalIndex>(InvalidPrefabLocalIndex);
				const YAML::Node clipsNode = animatorNode["AnimationClips"].IsDefined()
					? animatorNode["AnimationClips"] : animatorNode["Clips"];
				if (clipsNode)
				{
					for (const auto& clip : clipsNode)
						a.Clips.emplace_back(AssetHandle(clip.as<uint64_t>()));
				}
				a.ActiveClip = animatorNode["ActiveClip"].as<int32_t>(-1);
				a.PlaybackSpeed = animatorNode["PlaybackSpeed"].as<float>(1.0f);
				a.Loop = animatorNode["Loop"].as<bool>(true);
				a.AutoPlay = animatorNode["AutoPlay"].as<bool>(false);
				e.Animator = a;
			}
			else if (const YAML::Node& legacyAnimationNode = n["Animation"])
			{
				// Migrate the old single-clip animation record into the animator.
				AnimatorComponentTemplate a;
				const AssetHandle clip = AssetHandle(legacyAnimationNode["AnimationAsset"].as<uint64_t>(0));
				if (clip.IsValid())
					a.Clips.push_back(clip);
				a.ActiveClip = a.Clips.empty() ? -1 : 0;
				a.PlaybackSpeed = legacyAnimationNode["PlaybackSpeed"].as<float>(1.0f);
				a.Loop = legacyAnimationNode["Loop"].as<bool>(true);
				a.AutoPlay = legacyAnimationNode["AutoPlay"].as<bool>(true);
				e.Animator = a;
			}

			if (const YAML::Node& rigidBodyNode = n["RigidBody"])
			{
				RigidBody3DComponent r;
				r.Type = static_cast<RigidBody3DComponent::BodyType>(rigidBodyNode["Type"].as<int>(1));
				r.Mass = rigidBodyNode["Mass"].as<float>(1.0f);
				r.UseGravity = rigidBodyNode["UseGravity"].as<bool>(true);
				r.RuntimeBody = nullptr;
				e.RigidBody = r;
			}

			if (n["Scripts"])
			{
				for (const auto& scriptNode : n["Scripts"])
				{
					PrefabScriptTemplate script;
					script.ClassName =
						ScriptUtils::MigrateLegacyScriptClassName(scriptNode["ClassName"].as<std::string>());

					if (auto scriptFields = scriptNode["ScriptFields"])
					{
						for (const auto& fieldNode : scriptFields)
						{
							ScriptFieldInitializer initializer;
							initializer.Field.Name = fieldNode["Name"].as<std::string>();
							initializer.Field.Type =
								ScriptUtils::StringToScriptFieldType(fieldNode["Type"].as<std::string>());
							ScriptFieldSerializer::Deserialize(fieldNode, initializer);
							script.FieldInitializers.push_back(std::move(initializer));
						}
					}

					e.Scripts.push_back(std::move(script));
				}
			}

			prefab->Entities.push_back(std::move(e));
		}
		std::unordered_map<UUID, PrefabLocalIndex> ids;
		for (const auto& e : prefab->Entities)
		{
            if (e.ID.IsValid()) 
			{
                ids[e.ID] = e.LocalIndex;
            }
		}
		for (auto& e : prefab->Entities)
		{
			if (e.ParentLocalIndex == InvalidPrefabLocalIndex && ids.contains(e.Parent))
			{
				e.ParentLocalIndex = ids[e.Parent];
			}
			for (const auto& id : e.ChildIDs)
			{
				if (ids.contains(id)) 
					e.Children.push_back(ids[id]);
			}
		}
		if (prefab->RootLocalIndex == InvalidPrefabLocalIndex)
		{
            for (const auto& e : prefab->Entities) 
			{
                if (e.ID == prefab->RootEntityID) 
				{
                    prefab->RootLocalIndex = e.LocalIndex;
                    break;
                }
            }

		}
		return prefab;
	}
}