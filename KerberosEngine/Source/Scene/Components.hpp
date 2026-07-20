#pragma once

#include <string>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Assets/Asset.hpp"
#include "Assets/AssetManager.hpp"
#include "Renderer/Mesh.hpp"
#include "Renderer/Lights.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Textures/Texture.hpp"
#include "Renderer/Textures/TextureCube.hpp"
#include "Scene/Camera/SceneCamera.hpp"
#include "Scene/AABB.hpp"
#include "Renderer/Font.hpp"
#include "Core/UUID.hpp"

namespace Kerberos
{
	class ScriptableEntity;

	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		~IDComponent() = default;
		IDComponent(const IDComponent&) = default;
		IDComponent(IDComponent&&) = default;
		IDComponent& operator=(const IDComponent&) = default;
		IDComponent& operator=(IDComponent&&) = default;
	};

	struct TransformComponent
	{
		glm::vec3 Translation = glm::vec3(0.0f);
		glm::vec3 Rotation = glm::vec3(0.0f);
		glm::vec3 Scale = glm::vec3(1.0f);

		glm::mat4 WorldTransform = glm::mat4(1.0f);

		TransformComponent() = default;
		~TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(TransformComponent&&) = default;
		TransformComponent& operator=(const TransformComponent&) = default;
		TransformComponent& operator=(TransformComponent&&) = default;

		explicit TransformComponent(const glm::vec3& translation)
			: Translation(translation)
		{
		}

		glm::mat4 GetTransform() const
		{
			const glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

			const glm::mat4 transform = glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);

			return transform;
		}
	};

	struct SpriteRendererComponent
	{
		glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };

		SpriteRendererComponent() = default;
		~SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent&) = default;
		SpriteRendererComponent(SpriteRendererComponent&&) = default;
		SpriteRendererComponent& operator=(const SpriteRendererComponent&) = default;
		SpriteRendererComponent& operator=(SpriteRendererComponent&&) = default;

		explicit SpriteRendererComponent(const glm::vec4& color)
			: Color(color)
		{
		}
	};

	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		~TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(TagComponent&&) = default;
		TagComponent& operator=(const TagComponent&) = default;
		TagComponent& operator=(TagComponent&&) = default;
		TagComponent& operator=(const std::string& tag)
		{
			Tag = tag;
			return *this;
		}

		explicit TagComponent(std::string tag)
			: Tag(std::move(tag)) {
		}

		explicit operator std::string& () { return Tag; }
		explicit operator const std::string& () const { return Tag; }
		explicit operator const char* () const { return Tag.c_str(); }
	};

	struct CameraComponent
	{
		SceneCamera Camera;
		bool IsPrimary = true;

		/**
		* If true, the camera will maintain a fixed aspect ratio when the window is resized.
		*/
		bool FixedAspectRatio = false;

		CameraComponent() = default;
		explicit CameraComponent(const SceneCamera& camera)
			: Camera(camera)
		{
		}
		CameraComponent(const CameraComponent&) = default;
		CameraComponent(CameraComponent&&) = default;

		CameraComponent& operator=(const CameraComponent&) = default;
		CameraComponent& operator=(CameraComponent&&) = default;
		CameraComponent& operator=(const SceneCamera& camera)
		{
			Camera = camera;
			return *this;
		}

		explicit operator SceneCamera& () { return Camera; }

		~CameraComponent() = default;
	};

	struct ScriptComponent
	{
		std::string ClassName;

		ScriptComponent() = default;
		ScriptComponent(const ScriptComponent&) = default;
		ScriptComponent(ScriptComponent&&) = default;

		ScriptComponent& operator=(const ScriptComponent&) = default;
		ScriptComponent& operator=(ScriptComponent&&) = default;

		~ScriptComponent() = default;
	};

	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		std::function<void()> Instantiate;
		std::function<void()> Destroy;

		template<typename T>
		void Bind()
		{
			Instantiate = [&]() { Instance = new T(); };

			Destroy = [&]() {
				delete reinterpret_cast<T*>(Instance);
				Instance = nullptr;
			};
		}
	};

	struct StaticMeshComponent
	{
		Ref<Mesh> StaticMesh = nullptr;
		Ref<Material> MeshMaterial = nullptr;
		Ref<Texture2D> MeshTexture = nullptr;

		AABB WorldAABB;

		bool Visible = true;
		bool CastShadows = true;

		StaticMeshComponent()
		{
			//MeshMaterial = CreateRef<Material>();
			//StaticMesh = Mesh::CreateCube(1.0f);
		}

		StaticMeshComponent(const Ref<Mesh>& mesh, const Ref<Material>& material, const Ref<Texture2D>& texture = nullptr)
			: StaticMesh(mesh), MeshMaterial(material), MeshTexture(texture)
		{
		}
		StaticMeshComponent(const StaticMeshComponent&) = default;
	};

	struct ModelComponent
	{
		AssetHandle ModelAsset = AssetHandle::Invalid();
		bool Visible = true;

		ModelComponent() = default;
		explicit ModelComponent(const AssetHandle modelAsset)
			: ModelAsset(modelAsset)
		{
		}
		ModelComponent(const ModelComponent&) = default;
	};

	struct PrefabInstanceComponent
	{
		AssetHandle PrefabAsset = AssetHandle::Invalid();
		bool IsExpandedInHierarchy = true; // Editor UI state

		PrefabInstanceComponent() = default;
		explicit PrefabInstanceComponent(const AssetHandle prefab)
			: PrefabAsset(prefab)
		{
		}
	};

	struct DirectionalLightComponent
	{
		DirectionalLight Light;
		bool IsEnabled = true;
		bool CastShadows = true;

		/// Used to update the light's shadow map only when necessary
		bool NeedsUpdate = true;

		DirectionalLightComponent() = default;
		explicit DirectionalLightComponent(const DirectionalLight& light)
			: Light(light)
		{
		}
	};

	struct PointLightComponent
	{
		PointLight Light;
		bool IsEnabled = true;

		PointLightComponent() = default;
		explicit PointLightComponent(const PointLight& light)
			: Light(light)
		{
		}
	};

	struct SpotLightComponent
	{
		SpotLight Light;
		bool IsEnabled = true;

		SpotLightComponent() = default;
		explicit SpotLightComponent(const SpotLight& light)
			: Light(light)
		{
		}
	};

	struct HierarchyComponent
	{
		UUID Parent = UUID::Invalid();
		std::vector<UUID> Children;

		HierarchyComponent() = default;
	};

	struct EnvironmentComponent
	{
		AssetHandle SkyboxTexture = AssetHandle::Invalid();
		bool IsSkyboxEnabled = true;

		EnvironmentComponent() = default;
		explicit EnvironmentComponent(const AssetHandle& skyboxTexture, const bool isSkyboxEnabled = true)
			: SkyboxTexture(skyboxTexture), IsSkyboxEnabled(isSkyboxEnabled)
		{
		}
	};

	struct TextComponent
	{
		Ref<Font> Font = AssetManager::GetDefaultFont();
		std::string Text = "Sample Text";
		glm::vec4 Color = glm::vec4(1.0f);
		float FontSize = 12.0f;

		TextComponent() = default;
		explicit TextComponent(const Ref<Kerberos::Font>& font, std::string text)
			: Font(font), Text(std::move(text))
		{
		}
	};
}
