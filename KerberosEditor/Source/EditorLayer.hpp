#pragma once

#include "Layer.hpp"
#include "Vulkan.hpp"

#include "Windows/AssetsPanel.hpp"
#include "Windows/HierarchyPanel.hpp"

#include "Renderer/Mesh.hpp"
#include "Renderer/Textures/Texture2D.hpp"
#include "Renderer/Textures/TextureCube.hpp"
#include "Buffer.hpp"
#include "Renderer/Material.hpp"
#include "Core/Core.hpp"
#include "Scene/Node.hpp"
#include "Scene/Camera/Camera.hpp"
#include "Renderer/MaterialRegistry.hpp"
#include "Events/MouseButtonPressedEvent.hpp"
#include "Events/WindowDropEvent.hpp"

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include <unordered_map>



namespace Kerberos
{
	class EditorLayer : public Layer
	{
	public:
		EditorLayer();
		~EditorLayer() override;
		void OnAttach() override;
		void OnDetach() override;

		void OnUpdate(float deltaTime) override;
		void OnEvent(Event& event) override;
		void OnImGuiRender() override;

	private:
		void OnScenePlay();
		void OnSceneSimulate();
		void OnSceneStop();

		void HandleDragAndDrop();
		void HandleMousePicking();

		void NewProject();
		void OpenProject(const std::filesystem::path& filepath);
		[[nodiscard]] bool OpenProject();

		void SaveScene();
		void SaveSceneAs();

		/**
		 * Opens the file system dialog to select a scene file to open.
		 */
		void LoadScene();

		/**
		 * Loads a scene from the specified file path.
		 * @param filepath The path to the scene file to open.
		 */
		void OpenScene(const std::filesystem::path& filepath);
		void OpenScene(const Ref<Scene>& scene);
		void NewScene();

		void DrawViewport();
		void DrawUIToolbar();
		void DrawMenuBar();
		void DrawDebugWindow();

		void UpdateLights(float time, uint32_t currentImage);
		void UpdateSceneUniformBuffers(uint32_t currentImage);
		void UpdatePerObjectUniformBuffer(uint32_t currentImage, uint32_t objectIndex, const glm::mat4& model, const Material& material);

		glm::mat4 CalculateLightSpaceMatrix();

		void CalculateEntityTransform(const Entity& entity) const;

		bool OnKeyPressed(const KeyPressedEvent& event);
		bool OnMouseButtonPressed(const MouseButtonPressedEvent& event);
		bool OnWindowDrop(const WindowDropEvent& event);

	private:
		float m_Time = 0.0f;
		float m_Fps = 0.0f;

		HierarchyPanel m_HierarchyPanel;
		Owner<AssetsPanel> m_AssetsPanel;

		NotificationManager m_NotificationManager;

		Ref<Scene> m_ActiveScene;
		Ref<Scene> m_EditorScene;
		Ref<Scene> m_RuntimeScene;

		Entity m_CameraEntity;

		Entity m_HoveredEntity;

		Ref<Font> m_BasicFont;

		enum class SceneState : uint8_t
		{
			Edit,
			Play,
			Simulate
		};
		SceneState m_SceneState = SceneState::Edit;
		bool m_IsScenePaused = false;

		bool m_ViewportFocused = false;
		bool m_ViewportHovered = false;
		std::array<glm::vec2, 2> m_ViewportBounds;

		// From ImGuizmo.h
		enum class GizmoType : std::uint16_t
		{
			None = 0,
			Translate = 7,
			Rotate = 896,
			Scale = 120
		};
		GizmoType m_GizmoType = GizmoType::None;

		Ref<Texture2D> m_IconPlay;
		Ref<Texture2D> m_IconStop;
		Ref<Texture2D> m_IconPause;
		Ref<Texture2D> m_IconResume;

		bool m_IsFullScreenPersistent = true;

		bool m_DisplaySkybox = true;
		bool m_DisplayDebugNormals = false;
		bool m_EnablePCF = true;

		glm::vec3 m_LightPosForShadowMapCalculation{ 0.0f };

		Owner<Camera> m_EditorCamera = nullptr;
		// Size of the ImGui viewport.
		glm::vec2 m_ViewportSize{ 0.f };
		// Size of the output images.
		glm::vec2 m_OutputSize{ 0.f };

		std::unordered_map<std::string, Ref<Mesh>> m_Meshes;
		std::optional<Mesh> m_SkyboxMesh;
		std::vector<Ref<Texture2D>> m_Textures;
		int m_SelectedMaterialIndex = 0;

		std::vector<Owner<Node>> m_SceneNodes;

		MaterialRegistry m_MaterialRegistry;

		struct DepthBias
		{
			float constantFactor = 1.25f;
			float slopeFactor = 1.75f;
			float clamp = 0.0f;
		};
		DepthBias m_DepthBias;

		// Vulkan resources
		uint32_t m_ShadowMapSize = 2048;
		vk::raii::Image m_ShadowMapImage = nullptr;
		vk::raii::DeviceMemory m_ShadowMapImageMemory = nullptr;
		vk::raii::ImageView m_ShadowMapImageView = nullptr;
		vk::raii::PipelineLayout m_ShadowMapPipelineLayout = nullptr;
		vk::raii::Pipeline m_ShadowMapPipeline = nullptr;

		Ref<TextureCube> m_SkyboxTexture = CreateRef<TextureCube>();
		// Generated at runtime
		Ref<Texture2D> m_LutBrdfTexture = CreateRef<Texture2D>();
		Ref<TextureCube> m_IrradianceCubeTexture = CreateRef<TextureCube>();
		Ref<TextureCube> m_PrefilteredCubeTexture = CreateRef<TextureCube>();

		vk::raii::Image m_ColorImage = nullptr;
		vk::raii::DeviceMemory m_ColorImageMemory = nullptr;
		vk::raii::ImageView m_ColorImageView = nullptr;

		vk::raii::Image m_DepthImage = nullptr;
		vk::raii::DeviceMemory m_DepthImageMemory = nullptr;
		vk::raii::ImageView m_DepthImageView = nullptr;

		vk::raii::DescriptorPool m_DescriptorPool = nullptr;

		struct DescriptorSetLayouts
		{
			vk::raii::DescriptorSetLayout scene = nullptr;
			vk::raii::DescriptorSetLayout textures = nullptr;
		};
		DescriptorSetLayouts m_DescriptorSetLayouts;

		vk::raii::PipelineLayout m_PBRPipelineLayout = nullptr;
		vk::raii::Pipeline m_PBROpaquePipeline = nullptr;
		vk::raii::Pipeline m_PBROpaquePipelinePCF = nullptr;
		//vk::raii::Pipeline m_PBROpaqueTexturedPipeline = nullptr;
		vk::raii::Pipeline m_PBRTransparentPipeline = nullptr;
		vk::raii::Pipeline m_SkyboxPipeline = nullptr;
		vk::raii::Pipeline m_NormalDebugPipeline = nullptr;

		vk::raii::Sampler m_ColorSampler = nullptr;
		vk::raii::Sampler m_ShadowMapSampler = nullptr;

		struct SceneUniformData
		{
			glm::mat4 projection{ 0.f };
			glm::mat4 view{ 0.f };
			glm::mat4 lightSpaceMatrix{ 0.f };
			alignas(16) glm::vec3 ambientLightColor{ 0.1f, 0.1f, 0.1f };
			alignas(16) glm::vec3 camPos{ 0.f };
		};
		SceneUniformData m_SceneUniformData{};

		struct UniformDataParams
		{
			// Direction of the lights
			alignas(16) std::array<glm::vec4, 4> lights{};
			float exposure = 4.5f;
			float gamma = 2.2f;
		};
		UniformDataParams m_UniformDataParams{};

		struct PerObjectData
		{
			alignas(16) glm::mat4 model{ 0.f };
			alignas(16) glm::mat4 worldNormal{ 0.f };
			alignas(16) Material::UniformBlock material;
		};
		PerObjectData m_PerObjectUniformData{};

		struct SkyboxData
		{
			glm::mat4 projection{ 0.f };
			glm::mat4 model{ 0.f };
		};
		SkyboxData m_SkyboxData{};

		struct UniformBufferObject
		{
			std::shared_ptr<UniformBuffer> scene;
			std::shared_ptr<UniformBuffer> params;
			std::shared_ptr<UniformBuffer> perObject;
			std::shared_ptr<UniformBuffer> skybox;
		};
		// TODO: This should hold multiple UBOs for multiple frames in flight
		std::array<UniformBufferObject, 1> m_UniformBuffers;

		struct DescriptorSets
		{
			vk::raii::DescriptorSet scene = nullptr;
			vk::raii::DescriptorSet skybox = nullptr;
		};

		// TODO: This should hold multiple descriptor sets for multiple frames in flight
		std::array<DescriptorSets, 1> m_DescriptorSets{};

		VkDescriptorSet m_ColorOutputDescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSet m_ShadowMapDescriptorSet = VK_NULL_HANDLE;

		// Dynamic uniform buffer related members
		VkDeviceSize m_MinUniformBufferOffsetAlignment = 0;
		uint64_t m_DynamicAlignment = 0;
		constexpr static size_t MaxObjects = 1000;
	};

}
