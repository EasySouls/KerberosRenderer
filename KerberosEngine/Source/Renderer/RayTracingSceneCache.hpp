#pragma once

#include "Vulkan.hpp"
#include "VulkanContext.hpp"
#include "Assets/Asset.hpp"

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>


namespace Kerberos
{
	class Scene;

	struct BLAS
	{
		vk::raii::Buffer Buffer = nullptr;
		vk::raii::DeviceMemory Memory = nullptr;
		vk::raii::AccelerationStructureKHR Handle = nullptr;
	};

	struct Instances
	{
		vk::raii::Buffer Buffer = nullptr;
		vk::raii::DeviceMemory Memory = nullptr;
		std::vector<vk::AccelerationStructureInstanceKHR> InstanceData;
	};

	struct TLAS
	{
		vk::raii::Buffer ScratchBuffer = nullptr;
		vk::raii::DeviceMemory ScratchMemory = nullptr;
		vk::raii::Buffer Buffer = nullptr;
		vk::raii::DeviceMemory Memory = nullptr;
		vk::raii::AccelerationStructureKHR Handle = nullptr;
	};

	class RayTracingSceneCache
	{
	public:
		RayTracingSceneCache() = default;
		~RayTracingSceneCache() = default;

		void BuildAccelerationStructures(const Ref<Scene>& scene);

		vk::AccelerationStructureKHR GetTLAS(uint32_t frameIndex) const;

	private:
		void UpdateTLAS(const Ref<Scene>& scene);

	private:
		std::vector<BLAS> m_BLASCache;
		Instances m_InstancesCache;
		std::array<TLAS, VulkanContext::MaxFramesInFlight> m_TLASCache;
	};
}
