#pragma once

#include "Assets/Asset.hpp"
#include "Renderer/Material.hpp"
#include "Renderer/Mesh.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Kerberos
{
	enum class ModelAnimationPath : uint8_t
	{
		Translation = 0,
		Rotation,
		Scale,
		Weights
	};

	struct ModelPrimitive
	{
		std::string Name;
		Ref<Mesh> Mesh = nullptr;
		Ref<Material> Material = nullptr;

		int32_t SourceMeshIndex = -1;
		int32_t SourcePrimitiveIndex = -1;
	};

	struct ModelNode
	{
		std::string Name;

		int32_t ParentIndex = -1;
		std::vector<int32_t> Children;

		glm::vec3 Translation = glm::vec3(0.0f);
		glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec3 Scale = glm::vec3(1.0f);

		std::vector<uint32_t> PrimitiveIndices;
		int32_t SkinIndex = -1;
	};

	struct ModelSkin
	{
		std::string Name;
		int32_t SkeletonRootNode = -1;
		std::vector<int32_t> JointNodes;
		std::vector<glm::mat4> InverseBindMatrices;
	};

	struct ModelAnimationSampler
	{
		std::vector<float> Inputs;
		std::vector<glm::vec4> Outputs;
		std::string Interpolation = "LINEAR";
	};

	struct ModelAnimationChannel
	{
		uint32_t SamplerIndex = 0;
		int32_t NodeIndex = -1;
		ModelAnimationPath Path = ModelAnimationPath::Translation;
	};

	struct ModelAnimationClip
	{
		std::string Name;
		std::vector<ModelAnimationSampler> Samplers;
		std::vector<ModelAnimationChannel> Channels;
	};

	class Model : public Asset
	{
	public:
		Model() = default;
		explicit Model(std::string name)
			: m_Name(std::move(name))
		{
		}

		AssetType GetType() override { return AssetType::Model; }

		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		const std::vector<ModelPrimitive>& GetPrimitives() const { return m_Primitives; }
		const std::vector<ModelNode>& GetNodes() const { return m_Nodes; }
		const std::vector<Ref<Material>>& GetMaterials() const { return m_Materials; }
		const std::vector<ModelSkin>& GetSkins() const { return m_Skins; }
		const std::vector<ModelAnimationClip>& GetAnimations() const { return m_Animations; }

		std::vector<ModelPrimitive>& GetPrimitives() { return m_Primitives; }
		std::vector<ModelNode>& GetNodes() { return m_Nodes; }
		std::vector<Ref<Material>>& GetMaterials() { return m_Materials; }
		std::vector<ModelSkin>& GetSkins() { return m_Skins; }
		std::vector<ModelAnimationClip>& GetAnimations() { return m_Animations; }

		Ref<Mesh> GetPrimaryMesh() const
		{
			for (const auto& primitive : m_Primitives)
			{
				if (primitive.Mesh)
					return primitive.Mesh;
			}

			return nullptr;
		}

	private:
		std::string m_Name;
		std::vector<ModelPrimitive> m_Primitives;
		std::vector<ModelNode> m_Nodes;
		std::vector<Ref<Material>> m_Materials;
		std::vector<ModelSkin> m_Skins;
		std::vector<ModelAnimationClip> m_Animations;
	};
}
