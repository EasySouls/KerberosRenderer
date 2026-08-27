#include "kbrpch.hpp"
#include "PrefabSerializer.hpp"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <unordered_map>

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
		if (!prefab) return false;
		YAML::Emitter out;
		out << YAML::BeginMap << YAML::Key << "Prefab" << YAML::Value << YAML::BeginMap;
		out << YAML::Key << "Name" << YAML::Value << prefab->Name;
		out << YAML::Key << "RootLocalIndex" << YAML::Value << prefab->RootLocalIndex;
		out << YAML::EndMap << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		for (size_t i = 0; i < prefab->Entities.size(); ++i)
		{
			const auto& e = prefab->Entities[i];
			const auto index = e.LocalIndex == InvalidPrefabLocalIndex ? static_cast<PrefabLocalIndex>(i) : e.LocalIndex;
			out << YAML::BeginMap << YAML::Key << "LocalIndex" << YAML::Value << index;
			out << YAML::Key << "Name" << YAML::Value << (e.Name.empty() ? e.Tag : e.Name);
			out << YAML::Key << "ParentLocalIndex" << YAML::Value << e.ParentLocalIndex;
			out << YAML::Key << "Children" << YAML::Value << YAML::Flow << YAML::BeginSeq;
			for (auto child : e.Children) out << child;
			out << YAML::EndSeq;
			auto writeVec = [&out](const char* key, const glm::vec3& v) {
				out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
			};
			writeVec("Translation", e.Transform.Translation);
			writeVec("Rotation", e.Transform.Rotation);
			writeVec("Scale", e.Transform.Scale);
			if (e.Skin)
			{
				out << YAML::Key << "Skin" << YAML::Value << YAML::BeginMap;
				WriteHandle(out, "SkeletonAsset", e.Skin->SkeletonAsset);
				out << YAML::Key << "JointEntityIndices" << YAML::Value << YAML::Flow << YAML::BeginSeq;
				for (auto joint : e.Skin->JointEntityIndices) out << joint;
				out << YAML::EndSeq << YAML::EndMap;
			}
			if (e.SkeletalMesh)
			{
				out << YAML::Key << "SkeletalMesh" << YAML::Value << YAML::BeginMap;
				WriteHandle(out, "MeshAsset", e.SkeletalMesh->MeshAsset);
				WriteHandle(out, "SkeletonAsset", e.SkeletalMesh->SkeletonAsset);
				WriteHandle(out, "MaterialAsset", e.SkeletalMesh->MaterialAsset);
				out << YAML::Key << "Visible" << YAML::Value << e.SkeletalMesh->Visible
					<< YAML::Key << "CastShadows" << YAML::Value << e.SkeletalMesh->CastShadows << YAML::EndMap;
			}
			if (e.Animation)
			{
				out << YAML::Key << "Animation" << YAML::Value << YAML::BeginMap;
				WriteHandle(out, "AnimationAsset", e.Animation->AnimationAsset);
				out << YAML::Key << "PlaybackSpeed" << YAML::Value << e.Animation->PlaybackSpeed
					<< YAML::Key << "Loop" << YAML::Value << e.Animation->Loop
					<< YAML::Key << "AutoPlay" << YAML::Value << e.Animation->AutoPlay << YAML::EndMap;
			}
			if (e.RigidBody)
			{
				out << YAML::Key << "RigidBody" << YAML::Value << YAML::BeginMap
					<< YAML::Key << "Type" << YAML::Value << static_cast<int>(e.RigidBody->Type)
					<< YAML::Key << "Mass" << YAML::Value << e.RigidBody->Mass
					<< YAML::Key << "UseGravity" << YAML::Value << e.RigidBody->UseGravity
					<< YAML::EndMap;
			}
			out << YAML::Key << "HasStaticMesh" << YAML::Value << e.HasStaticMesh
				<< YAML::Key << "MeshAssetPath" << YAML::Value << e.MeshAssetPath
				<< YAML::Key << "MaterialAssetPath" << YAML::Value << e.MaterialAssetPath
				<< YAML::EndMap;
		}
		out << YAML::EndSeq << YAML::EndMap;
		std::ofstream file(filepath);
		if (!file) return false;
		file << out.c_str();
		return true;
	}

	Ref<Prefab> PrefabSerializer::DeserializePrefab(const std::filesystem::path& filepath)
	{
		if (!std::filesystem::exists(filepath)) return nullptr;
		YAML::Node root;
		try { root = YAML::LoadFile(filepath.string()); }
		catch (const YAML::Exception& e) { KBR_CORE_ERROR("Prefab YAML parse failed: {}", e.what()); return nullptr; }
		if (!root["Prefab"] || !root["Entities"]) return nullptr;

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
			e.ParentLocalIndex = n["ParentLocalIndex"].as<PrefabLocalIndex>(InvalidPrefabLocalIndex);
			e.Parent = UUID(n["Parent"].as<uint64_t>(0));
			if (n["Children"]) for (const auto& c : n["Children"]) {
				if (n["ParentLocalIndex"].IsDefined() || n["LocalIndex"].IsDefined()) e.Children.push_back(c.as<PrefabLocalIndex>());
				else e.ChildIDs.emplace_back(c.as<uint64_t>());
			}
			e.HasStaticMesh = n["HasStaticMesh"].as<bool>(false);
			e.MeshAssetPath = n["MeshAssetPath"].as<std::string>("");
			e.MaterialAssetPath = n["MaterialAssetPath"].as<std::string>("");
			if (n["Skin"]) {
				SkinComponentTemplate s;
				s.SkeletonAsset = AssetHandle(n["Skin"]["SkeletonAsset"].as<uint64_t>(0));
				if (n["Skin"]["JointEntityIndices"]) for (const auto& j : n["Skin"]["JointEntityIndices"]) s.JointEntityIndices.push_back(j.as<PrefabLocalIndex>());
				e.Skin = s;
			}
			if (n["SkeletalMesh"]) {
				SkeletalMeshComponentTemplate s;
				s.MeshAsset = AssetHandle(n["SkeletalMesh"]["MeshAsset"].as<uint64_t>(0));
				s.SkeletonAsset = AssetHandle(n["SkeletalMesh"]["SkeletonAsset"].as<uint64_t>(0));
				s.MaterialAsset = AssetHandle(n["SkeletalMesh"]["MaterialAsset"].as<uint64_t>(0));
				s.Visible = n["SkeletalMesh"]["Visible"].as<bool>(true);
				s.CastShadows = n["SkeletalMesh"]["CastShadows"].as<bool>(true);
				e.SkeletalMesh = s;
			}
			if (n["Animation"]) {
				AnimationComponentTemplate a;
				a.AnimationAsset = AssetHandle(n["Animation"]["AnimationAsset"].as<uint64_t>(0));
				a.PlaybackSpeed = n["Animation"]["PlaybackSpeed"].as<float>(1.0f);
				a.Loop = n["Animation"]["Loop"].as<bool>(true);
				a.AutoPlay = n["Animation"]["AutoPlay"].as<bool>(true);
				e.Animation = a;
			}
			if (n["RigidBody"])
			{
				RigidBody3DComponent r;
				r.Type = static_cast<RigidBody3DComponent::BodyType>(n["RigidBody"]["Type"].as<int>(1));
				r.Mass = n["RigidBody"]["Mass"].as<float>(1.0f);
				r.UseGravity = n["RigidBody"]["UseGravity"].as<bool>(true);
				r.RuntimeBody = nullptr;
				e.RigidBody = r;
			}
			prefab->Entities.push_back(std::move(e));
		}
		std::unordered_map<UUID, PrefabLocalIndex> ids;
		for (const auto& e : prefab->Entities) if (e.ID.IsValid()) ids[e.ID] = e.LocalIndex;
		for (auto& e : prefab->Entities)
		{
			if (e.ParentLocalIndex == InvalidPrefabLocalIndex && ids.contains(e.Parent))
				e.ParentLocalIndex = ids[e.Parent];
			for (const auto& id : e.ChildIDs) if (ids.contains(id)) e.Children.push_back(ids[id]);
		}
		if (prefab->RootLocalIndex == InvalidPrefabLocalIndex)
			for (const auto& e : prefab->Entities) if (e.ID == prefab->RootEntityID) { prefab->RootLocalIndex = e.LocalIndex; break; }
		return prefab;
	}
}
