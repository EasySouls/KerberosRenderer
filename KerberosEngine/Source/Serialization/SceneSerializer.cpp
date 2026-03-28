#include "kbrpch.hpp"
#include "SceneSerializer.hpp"

#include "Scene/Entity.hpp"
#include "Scene/Components.hpp"
#include "Scene/Components/PhysicsComponents.hpp"
#include "Scene/Components/AudioComponents.hpp"
#include "Assets/AssetManager.hpp"
//#include "Scripting/ScriptEngine.hpp"
//#include "Scripting/ScriptUtils.hpp"
//#include "Scripting/ScriptClass.hpp"
#include "SerializationUtils.hpp"

#include <yaml-cpp/yaml.h>
#include <glm/glm.hpp>

#include <fstream>

namespace Kerberos
{
	static void SerializeEntity(YAML::Emitter& out, const Entity entity)
	{
		out << YAML::BeginMap;

		out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap;
			const auto& tag = entity.GetComponent<TagComponent>().Tag;
			out << YAML::Key << "Tag" << YAML::Value << tag;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<TransformComponent>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap;

			const auto& transform = entity.GetComponent<TransformComponent>();
			out << YAML::Key << "Translation" << YAML::Value << transform.Translation;
			out << YAML::Key << "Rotation" << YAML::Value << transform.Rotation;
			out << YAML::Key << "Scale" << YAML::Value << transform.Scale;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<SpriteRendererComponent>())
		{
			out << YAML::Key << "SpriteRendererComponent";
			out << YAML::BeginMap;

			const auto& color = entity.GetComponent<SpriteRendererComponent>().Color;
			out << YAML::Key << "Color" << YAML::Value << color;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<CameraComponent>())
		{
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

		if (entity.HasComponent<HierarchyComponent>())
		{
			out << YAML::Key << "HierarchyComponent";
			out << YAML::BeginMap;
			const auto& hierarchy = entity.GetComponent<HierarchyComponent>();
			out << YAML::Key << "Parent" << YAML::Value << hierarchy.Parent;
			out << YAML::Key << "Children" << YAML::Value << YAML::BeginSeq;
			for (const auto& child : hierarchy.Children)
			{
				out << child;
			}
			out << YAML::EndSeq;
			out << YAML::EndMap;
		}

		/*if (entity.HasComponent<ScriptComponent>())
		{
			out << YAML::Key << "ScriptComponent";
			out << YAML::BeginMap;
			const auto& script = entity.GetComponent<ScriptComponent>();
			out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;

			out << YAML::Key << "ScriptFields" << YAML::Key;
			out << YAML::BeginSeq;
			{
				const auto& fieldInitializers = ScriptEngine::GetScriptFieldInitializerMap(entity);

				if (!fieldInitializers.empty())
				{
					for (const auto& [name, field] : fieldInitializers)
					{
						out << YAML::BeginMap;
						out << YAML::Key << "Name" << YAML::Value << name;
						out << YAML::Key << "Type" << YAML::Value << ScriptUtils::ScriptFieldTypeToString(field.Field.Type);
						switch (field.Field.Type)
						{
							case ScriptFieldType::Short:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<short>();
								break;
							case ScriptFieldType::Long:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<long>();
								break;
							case ScriptFieldType::UShort:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint16_t>();
								break;
							case ScriptFieldType::UInt:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint32_t>();
								break;
							case ScriptFieldType::ULong:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint64_t>();
								break;
							case ScriptFieldType::Double:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<double>();
								break;
							case ScriptFieldType::Char:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<char>();
								break;
							case ScriptFieldType::Byte:
								out << YAML::Key << "Data" << YAML::Value << static_cast<int>(field.GetValue<uint8_t>());
								break;
							case ScriptFieldType::String:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<std::string>();
								break;
							case ScriptFieldType::Bool:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<bool>();
								break;
							case ScriptFieldType::Int:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<int>();
								break;
							case ScriptFieldType::Float:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<float>();
								break;
							case ScriptFieldType::Vec2:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<glm::vec2>();
								break;
							case ScriptFieldType::Vec3:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<glm::vec3>();
								break;
							case ScriptFieldType::Vec4:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<glm::vec4>();
								break;
							case ScriptFieldType::AssetHandle:
								out << YAML::Key << "Data" << YAML::Value << field.GetValue<uint8_t>();
								break;
						}
						out << YAML::EndMap;
					}
				}

			}
			out << YAML::EndSeq;

			out << YAML::EndMap;
		}*/

		if (entity.HasComponent<NativeScriptComponent>())
		{

		}

		if (entity.HasComponent<DirectionalLightComponent>())
		{
			out << YAML::Key << "DirectionalLightComponent";
			out << YAML::BeginMap;
			const auto& directionalLight = entity.GetComponent<DirectionalLightComponent>();
			out << YAML::Key << "Color" << YAML::Value << directionalLight.Light.Color;
			out << YAML::Key << "Direction" << YAML::Value << directionalLight.Light.Direction;
			out << YAML::Key << "Intensity" << YAML::Value << directionalLight.Light.Intensity;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<PointLightComponent>())
		{
			out << YAML::Key << "PointLightComponent";
			out << YAML::BeginMap;
			const auto& pointLight = entity.GetComponent<PointLightComponent>();
			out << YAML::Key << "Color" << YAML::Value << pointLight.Light.Color;
			out << YAML::Key << "Position" << YAML::Value << pointLight.Light.Position;
			out << YAML::Key << "Intensity" << YAML::Value << pointLight.Light.Intensity;
			out << YAML::Key << "Constant" << YAML::Value << pointLight.Light.Constant;
			out << YAML::Key << "Linear" << YAML::Value << pointLight.Light.Linear;
			out << YAML::Key << "Quadratic" << YAML::Value << pointLight.Light.Quadratic;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<SpotLightComponent>())
		{
			out << YAML::Key << "SpotLightComponent";
			out << YAML::BeginMap;
			const auto& spotLight = entity.GetComponent<SpotLightComponent>();
			out << YAML::Key << "Color" << YAML::Value << spotLight.Light.Color;
			out << YAML::Key << "Position" << YAML::Value << spotLight.Light.Position;
			out << YAML::Key << "Direction" << YAML::Value << spotLight.Light.Direction;
			out << YAML::Key << "Intensity" << YAML::Value << spotLight.Light.Intensity;
			out << YAML::Key << "Constant" << YAML::Value << spotLight.Light.Constant;
			out << YAML::Key << "Linear" << YAML::Value << spotLight.Light.Linear;
			out << YAML::Key << "Quadratic" << YAML::Value << spotLight.Light.Quadratic;
			out << YAML::Key << "CutOffAngleRadians" << YAML::Value << spotLight.Light.CutOffAngleRadians;
			out << YAML::Key << "OuterCutOffAngleRadians" << YAML::Value << spotLight.Light.OuterCutOffAngleRadians;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<RigidBody3DComponent>())
		{
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

		if (entity.HasComponent<BoxCollider3DComponent>())
		{
			out << YAML::Key << "BoxCollider3DComponent";
			out << YAML::BeginMap;
			const auto& boxCollider = entity.GetComponent<BoxCollider3DComponent>();
			out << YAML::Key << "Size" << YAML::Value << boxCollider.Size;
			out << YAML::Key << "Offset" << YAML::Value << boxCollider.Offset;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<SphereCollider3DComponent>())
		{
			out << YAML::Key << "SphereCollider3DComponent";
			out << YAML::BeginMap;
			const auto& sphereCollider = entity.GetComponent<SphereCollider3DComponent>();
			out << YAML::Key << "Radius" << YAML::Value << sphereCollider.Radius;
			out << YAML::Key << "Offset" << YAML::Value << sphereCollider.Offset;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<CapsuleCollider3DComponent>())
		{
			out << YAML::Key << "CapsuleCollider3DComponent";
			out << YAML::BeginMap;
			const auto& capsuleCollider = entity.GetComponent<CapsuleCollider3DComponent>();
			out << YAML::Key << "Radius" << YAML::Value << capsuleCollider.Radius;
			out << YAML::Key << "Height" << YAML::Value << capsuleCollider.Height;
			out << YAML::Key << "Offset" << YAML::Value << capsuleCollider.Offset;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<MeshCollider3DComponent>())
		{
			out << YAML::Key << "MeshCollider3DComponent";
			out << YAML::BeginMap;
			const auto& meshCollider = entity.GetComponent<MeshCollider3DComponent>();
			out << YAML::Key << "Mesh" << YAML::Value << (meshCollider.Mesh ? meshCollider.Mesh->GetHandle() : UUID::Invalid());
			out << YAML::Key << "IsTrigger" << YAML::Value << meshCollider.IsTrigger;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<StaticMeshComponent>())
		{
			out << YAML::Key << "StaticMeshComponent";
			out << YAML::BeginMap;
			const auto& staticMesh = entity.GetComponent<StaticMeshComponent>();
			out << YAML::Key << "Mesh" << YAML::Value << (staticMesh.StaticMesh ? staticMesh.StaticMesh->GetHandle() : UUID::Invalid());
			if (auto& mat = staticMesh.MeshMaterial)
			{
				//out << YAML::Key << "Material" << YAML::Value << staticMesh.MeshMaterial->GetHandle();
				/*out << YAML::Key << "Material";
				out << YAML::BeginMap;

				out << YAML::Key << "Ambient" << YAML::Value << mat->Ambient;
				out << YAML::Key << "Diffuse" << YAML::Value << mat->Diffuse;
				out << YAML::Key << "Specular" << YAML::Value << mat->Specular;
				out << YAML::Key << "Shininess" << YAML::Value << mat->Shininess;

				out << YAML::EndMap;*/
			}
			else
			{
				out << YAML::Key << "Material" << YAML::Value << UUID::Invalid();
			}

			if (staticMesh.MeshTexture)
				out << YAML::Key << "Texture" << YAML::Value << staticMesh.MeshTexture->GetHandle();
			else
				out << YAML::Key << "Texture" << YAML::Value << UUID::Invalid();
			out << YAML::EndMap;
		}

		if (entity.HasComponent<EnvironmentComponent>())
		{
			out << YAML::Key << "EnvironmentComponent";
			out << YAML::BeginMap;
			const auto& environmentComponent = entity.GetComponent<EnvironmentComponent>();
			out << YAML::Key << "SkyboxTexture" << YAML::Value << environmentComponent.SkyboxTexture;
			out << YAML::Key << "IsSkyboxEnabled" << YAML::Value << environmentComponent.IsSkyboxEnabled;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<TextComponent>())
		{
			out << YAML::Key << "TextComponent";
			out << YAML::BeginMap;
			const auto& textComponent = entity.GetComponent<TextComponent>();
			out << YAML::Key << "Text" << YAML::Value << textComponent.Text;
			out << YAML::Key << "FontSize" << YAML::Value << textComponent.FontSize;
			out << YAML::Key << "Color" << YAML::Value << textComponent.Color;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<AudioSource3DComponent>())
		{
			out << YAML::Key << "AudioSource3DComponent";
			out << YAML::BeginMap;
			const auto& audioSource = entity.GetComponent<AudioSource3DComponent>();
			out << YAML::Key << "SoundAsset" << YAML::Value << (audioSource.SoundAsset ? audioSource.SoundAsset->GetHandle() : UUID::Invalid());
			out << YAML::Key << "Loop" << YAML::Value << audioSource.Loop;
			out << YAML::Key << "Volume" << YAML::Value << audioSource.Volume;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<AudioSource2DComponent>())
		{
			out << YAML::Key << "AudioSource2DComponent";
			out << YAML::BeginMap;
			const auto& audioSource = entity.GetComponent<AudioSource2DComponent>();
			out << YAML::Key << "SoundAsset" << YAML::Value << (audioSource.SoundAsset ? audioSource.SoundAsset->GetHandle() : UUID::Invalid());
			out << YAML::Key << "Loop" << YAML::Value << audioSource.Loop;
			out << YAML::Key << "Volume" << YAML::Value << audioSource.Volume;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<AudioListenerComponent>())
		{
			out << YAML::Key << "AudioListenerComponent";
			out << YAML::BeginMap;
			const auto& audioListener = entity.GetComponent<AudioListenerComponent>();
			out << YAML::Key << "Volume" << YAML::Value << audioListener.Volume;
			out << YAML::EndMap;
		}

		out << YAML::EndMap;

	}

	void SceneSerializer::Serialize(const std::filesystem::path& filepath) const
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Unnamed Scene";
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		for (const auto entityId : m_Scene->m_Registry.view<entt::entity>())
		{
			const Entity entity{ entityId, m_Scene.get() };
			if (!entity)
				continue;

			SerializeEntity(out, entity);
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		/// Check if there's a parent directory
		if (const std::filesystem::path parentDir = filepath.parent_path(); !parentDir.empty())
		{
			std::error_code ec;
			std::filesystem::create_directories(parentDir, ec);
			if (ec)
			{
				KBR_CORE_ERROR("Could not create directory {0}: {1}", parentDir.string(), ec.message());
			}
		}

		if (std::ofstream fileOut(filepath); fileOut)
		{
			fileOut << out.c_str();
			fileOut.close();
		}
		else
		{
			KBR_CORE_ERROR("Could not open file {0} for writing!", filepath.string());
		}
	}

	void SceneSerializer::SerializeRuntime(const std::filesystem::path& filepath)
	{
		throw std::logic_error("Not implemented");
	}

	bool SceneSerializer::Deserialize(const std::filesystem::path& filepath) const
	{
		const std::ifstream inFile(filepath);
		std::stringstream stream;
		stream << inFile.rdbuf();

		YAML::Node data = YAML::Load(stream.str());
		if (!data["Scene"])
		{
			KBR_CORE_ERROR("Invalid Scene file {0}", filepath.string());
			return false;
		}

		auto sceneName = data["Scene"].as<std::string>();

		if (auto entities = data["Entities"])
		{
			for (const auto& entity : entities)
			{
				uint64_t uuid = entity["Entity"].as<uint64_t>();
				std::string tag;
				if (entity["TagComponent"])
					tag = entity["TagComponent"]["Tag"].as<std::string>();

				/// Create the entity with the given tag and id
				Entity deserializedEntity = m_Scene->CreateEntityWithUUID(tag, uuid);

				if (auto transformComponent = entity["TransformComponent"])
				{
					auto& transform = deserializedEntity.GetComponent<TransformComponent>();
					transform.Translation = transformComponent["Translation"].as<glm::vec3>();
					transform.Rotation = transformComponent["Rotation"].as<glm::vec3>();
					transform.Scale = transformComponent["Scale"].as<glm::vec3>();
				}

				if (auto cameraComponent = entity["CameraComponent"])
				{
					auto& comp = deserializedEntity.AddComponent<CameraComponent>();
					auto cameraData = cameraComponent["Camera"];

					comp.Camera.SetProjectionType(static_cast<SceneCamera::ProjectionType>(cameraData["ProjectionType"].as<int>()));

					comp.Camera.SetPerspectiveFov(cameraData["PerspectiveFOV"].as<float>());
					comp.Camera.SetPerspectiveNearClip(cameraData["PerspectiveNear"].as<float>());
					comp.Camera.SetPerspectiveFarClip(cameraData["PerspectiveFar"].as<float>());

					comp.Camera.SetOrthographicSize(cameraData["OrthographicSize"].as<float>());
					comp.Camera.SetOrthographicNearClip(cameraData["OrthographicNear"].as<float>());
					comp.Camera.SetOrthographicFarClip(cameraData["OrthographicFar"].as<float>());

					comp.IsPrimary = cameraComponent["IsPrimary"].as<bool>();
					comp.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
				}

				if (auto spriteRendererComponent = entity["SpriteRendererComponent"])
				{
					auto& spriteRenderer = deserializedEntity.AddComponent<SpriteRendererComponent>();
					spriteRenderer.Color = spriteRendererComponent["Color"].as<glm::vec4>();
				}

				if (auto hierarchyComponent = entity["HierarchyComponent"])
				{
					/// The entity must have a HierarchyComponent already when created
					auto& hierarchy = deserializedEntity.GetComponent<HierarchyComponent>();
					const UUID parentId = static_cast<UUID>(hierarchyComponent["Parent"].as<uint64_t>());

					hierarchy.Parent = parentId;
					if (parentId.IsValid())
					{
						/// If the entity has a parent, it should not be a root entity
						m_Scene->m_RootEntities.erase(static_cast<entt::entity>(deserializedEntity));
					}

					for (const auto& child : hierarchyComponent["Children"])
					{
						const uint64_t childUUID = child.as<uint64_t>();
						hierarchy.Children.emplace_back(childUUID);
					}
				}

				/*if (auto scriptComponent = entity["ScriptComponent"])
				{
					auto& script = deserializedEntity.AddComponent<ScriptComponent>();
					script.ClassName = scriptComponent["ClassName"].as<std::string>();
					std::unordered_map<std::string, ScriptFieldInitializer>& scriptFieldInitializers = ScriptEngine::GetScriptFieldInitializerMap(deserializedEntity);
					if (auto scriptFields = scriptComponent["ScriptFields"])
					{
						for (const auto& field : scriptFields)
						{
							const std::string name = field["Name"].as<std::string>();
							const ScriptFieldType type = ScriptUtils::StringToScriptFieldType(field["Type"].as<std::string>());
							ScriptFieldInitializer initializer;
							initializer.Field.Name = name;
							initializer.Field.Type = type;
							switch (type)
							{
								case ScriptFieldType::Short:
									initializer.SetValue<short>(field["Data"].as<short>());
									break;
								case ScriptFieldType::Long:
									initializer.SetValue<long>(field["Data"].as<long>());
									break;
								case ScriptFieldType::UShort:
									initializer.SetValue<uint16_t>(field["Data"].as<uint16_t>());
									break;
								case ScriptFieldType::UInt:
									initializer.SetValue<uint32_t>(field["Data"].as<uint32_t>());
									break;
								case ScriptFieldType::ULong:
									initializer.SetValue<uint64_t>(field["Data"].as<uint64_t>());
									break;
								case ScriptFieldType::Double:
									initializer.SetValue<double>(field["Data"].as<double>());
									break;
								case ScriptFieldType::Char:
									initializer.SetValue<char>(field["Data"].as<char>());
									break;
								case ScriptFieldType::Byte:
									initializer.SetValue<uint8_t>(static_cast<uint8_t>(field["Data"].as<int>()));
									break;
								case ScriptFieldType::String:
									initializer.SetValue<std::string>(field["Data"].as<std::string>());
									break;
								case ScriptFieldType::Bool:
									initializer.SetValue<bool>(field["Data"].as<bool>());
									break;
								case ScriptFieldType::Int:
									initializer.SetValue<int>(field["Data"].as<int>());
									break;
								case ScriptFieldType::Float:
									initializer.SetValue<float>(field["Data"].as<float>());
									break;
								case ScriptFieldType::Vec2:
									initializer.SetValue<glm::vec2>(field["Data"].as<glm::vec2>());
									break;
								case ScriptFieldType::Vec3:
									initializer.SetValue<glm::vec3>(field["Data"].as<glm::vec3>());
									break;
								case ScriptFieldType::Vec4:
									initializer.SetValue<glm::vec4>(field["Data"].as<glm::vec4>());
									break;
								case ScriptFieldType::AssetHandle:
									initializer.SetValue<uint8_t>(field["Data"].as<uint8_t>());
									break;
							}

							scriptFieldInitializers[name] = initializer;
						}
					}
				}*/

				if (auto nativeScriptComponent = entity["NativeScriptComponent"])
				{
					// Handle NativeScriptComponent deserialization here
					// This is a placeholder as the actual implementation depends on the scripting system used
				}

				if (auto directionalLightComponent = entity["DirectionalLightComponent"])
				{
					auto& directionalLight = deserializedEntity.AddComponent<DirectionalLightComponent>();
					directionalLight.Light.Color = directionalLightComponent["Color"].as<glm::vec3>();
					directionalLight.Light.Direction = directionalLightComponent["Direction"].as<glm::vec3>();
					directionalLight.Light.Intensity = directionalLightComponent["Intensity"].as<float>();
				}

				if (auto pointLightComponent = entity["PointLightComponent"])
				{
					auto& pointLight = deserializedEntity.AddComponent<PointLightComponent>();
					pointLight.Light.Color = pointLightComponent["Color"].as<glm::vec3>();
					pointLight.Light.Position = pointLightComponent["Position"].as<glm::vec3>();
					pointLight.Light.Intensity = pointLightComponent["Intensity"].as<float>();
					pointLight.Light.Constant = pointLightComponent["Constant"].as<float>();
					pointLight.Light.Linear = pointLightComponent["Linear"].as<float>();
					pointLight.Light.Quadratic = pointLightComponent["Quadratic"].as<float>();
				}

				if (auto spotLightComponent = entity["SpotLightComponent"])
				{
					auto& spotLight = deserializedEntity.AddComponent<SpotLightComponent>();
					spotLight.Light.Color = spotLightComponent["Color"].as<glm::vec3>();
					spotLight.Light.Position = spotLightComponent["Position"].as<glm::vec3>();
					spotLight.Light.Direction = spotLightComponent["Direction"].as<glm::vec3>();
					spotLight.Light.Intensity = spotLightComponent["Intensity"].as<float>();
					spotLight.Light.Constant = spotLightComponent["Constant"].as<float>();
					spotLight.Light.Linear = spotLightComponent["Linear"].as<float>();
					spotLight.Light.Quadratic = spotLightComponent["Quadratic"].as<float>();
					spotLight.Light.CutOffAngleRadians = spotLightComponent["CutOffAngleRadians"].as<float>();
					spotLight.Light.OuterCutOffAngleRadians = spotLightComponent["OuterCutOffAngleRadians"].as<float>();
				}

				if (auto rigidBodyComponent = entity["RigidBody3DComponent"])
				{
					auto& rigidBody = deserializedEntity.AddComponent<RigidBody3DComponent>();
					rigidBody.Mass = rigidBodyComponent["Mass"].as<float>();
					rigidBody.Type = static_cast<RigidBody3DComponent::BodyType>(rigidBodyComponent["Type"].as<int>());
					rigidBody.Velocity = rigidBodyComponent["Velocity"].as<glm::vec3>();
					rigidBody.AngularVelocity = rigidBodyComponent["AngularVelocity"].as<glm::vec3>();
					rigidBody.UseGravity = rigidBodyComponent["UseGravity"].as<bool>();
					rigidBody.Friction = rigidBodyComponent["Friction"].as<float>();
					rigidBody.Restitution = rigidBodyComponent["Restitution"].as<float>();
				}

				if (auto boxColliderComponent = entity["BoxCollider3DComponent"])
				{
					auto& boxCollider = deserializedEntity.AddComponent<BoxCollider3DComponent>();
					boxCollider.Size = boxColliderComponent["Size"].as<glm::vec3>();
					boxCollider.Offset = boxColliderComponent["Offset"].as<glm::vec3>();
				}

				if (auto sphereColliderComponent = entity["SphereCollider3DComponent"])
				{
					auto& sphereCollider = deserializedEntity.AddComponent<SphereCollider3DComponent>();
					sphereCollider.Radius = sphereColliderComponent["Radius"].as<float>();
					sphereCollider.Offset = sphereColliderComponent["Offset"].as<glm::vec3>();
				}

				if (auto capsuleColliderComponent = entity["CapsuleCollider3DComponent"])
				{
					auto& capsuleCollider = deserializedEntity.AddComponent<CapsuleCollider3DComponent>();
					capsuleCollider.Radius = capsuleColliderComponent["Radius"].as<float>();
					capsuleCollider.Height = capsuleColliderComponent["Height"].as<float>();
					capsuleCollider.Offset = capsuleColliderComponent["Offset"].as<glm::vec3>();
				}

				if (auto meshColliderComponent = entity["MeshCollider3DComponent"])
				{
					auto& meshCollider = deserializedEntity.AddComponent<MeshCollider3DComponent>();
					meshCollider.IsTrigger = meshColliderComponent["IsTrigger"].as<bool>();
					const AssetHandle meshHandle = AssetHandle(meshColliderComponent["Mesh"].as<uint64_t>());
					if (meshHandle.IsValid())
					{
						meshCollider.Mesh = AssetManager::GetAsset<Mesh>(meshHandle);
					}
				}

				if (auto staticMeshComponent = entity["StaticMeshComponent"])
				{
					auto& staticMesh = deserializedEntity.AddComponent<StaticMeshComponent>();

					/// Get the material and texture handles
					/// If they are valid, load the assets, else use default ones

					/*const auto matNode = staticMeshComponent["Material"].as<std::uint64_t>();
					const AssetHandle materialHandle = UUID(matNode);
					if (materialHandle.IsValid())
						staticMesh.MeshMaterial = AssetManager::GetAsset<Material>(materialHandle);*/

					staticMesh.MeshMaterial = CreateRef<Material>();
					/*if (auto matNode = staticMeshComponent["Material"])
					{
						staticMesh.MeshMaterial->Ambient = matNode["Ambient"].as<glm::vec3>();
						staticMesh.MeshMaterial->Diffuse = matNode["Diffuse"].as<glm::vec3>();
						staticMesh.MeshMaterial->Specular = matNode["Specular"].as<glm::vec3>();
						staticMesh.MeshMaterial->Shininess = matNode["Shininess"].as<float>();
					}*/

					const AssetHandle textureHandle = AssetHandle(staticMeshComponent["Texture"].as<uint64_t>());
					if (textureHandle.IsValid())
						staticMesh.MeshTexture = AssetManager::GetAsset<Texture2D>(textureHandle);

					const AssetHandle meshHandle = AssetHandle(staticMeshComponent["Mesh"].as<uint64_t>());
					if (meshHandle.IsValid())
					{
						staticMesh.StaticMesh = AssetManager::GetAsset<Mesh>(meshHandle);
					}
					else
					{
						KBR_CORE_WARN("AssetHandle for mesh is invalid, using default cube mesh.");
						staticMesh.StaticMesh = AssetManager::GetDefaultCubeMesh();
					}
				}

				if (auto environmentComponent = entity["EnvironmentComponent"])
				{
					auto& environment = deserializedEntity.AddComponent<EnvironmentComponent>();
					environment.SkyboxTexture = AssetHandle(environmentComponent["SkyboxTexture"].as<uint64_t>());
					environment.IsSkyboxEnabled = environmentComponent["IsSkyboxEnabled"].as<bool>();
				}

				if (auto textComponent = entity["TextComponent"])
				{
					auto& text = deserializedEntity.AddComponent<TextComponent>();
					text.Text = textComponent["Text"].as<std::string>();
					text.Color = textComponent["Color"].as<glm::vec4>();
					text.FontSize = textComponent["FontSize"].as<float>();
				}

				if (auto audioSource3DComponent = entity["AudioSource3DComponent"])
				{
					auto& audioSource3D = deserializedEntity.AddComponent<AudioSource3DComponent>();
					audioSource3D.SoundAsset = AssetManager::GetAsset<Sound>(AssetHandle(audioSource3DComponent["SoundAsset"].as<uint64_t>()));
					audioSource3D.Loop = audioSource3DComponent["Loop"].as<bool>();
					audioSource3D.Volume = audioSource3DComponent["Volume"].as<float>();
				}

				if (auto audioSource2DComponent = entity["AudioSource2DComponent"])
				{
					auto& audioSource2D = deserializedEntity.AddComponent<AudioSource2DComponent>();
					audioSource2D.SoundAsset = AssetManager::GetAsset<Sound>(AssetHandle(audioSource2DComponent["SoundAsset"].as<uint64_t>()));
					audioSource2D.Loop = audioSource2DComponent["Loop"].as<bool>();
					audioSource2D.Volume = audioSource2DComponent["Volume"].as<float>();
				}

				if (auto audioListenerComponent = entity["AudioListenerComponent"])
				{
					auto& audioListener = deserializedEntity.AddComponent<AudioListenerComponent>();
					audioListener.Volume = audioListenerComponent["Volume"].as<float>();
				}
			}
		}

		return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::filesystem::path& filepath) const
	{
		throw std::logic_error("Not implemented");
	}
}