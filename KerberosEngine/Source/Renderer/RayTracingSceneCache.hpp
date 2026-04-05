#pragma once

#include "Vulkan.hpp"
#include "VulkanContext.hpp"

#include <unordered_map>
#include <vector>

namespace Kerberos
{
	class Scene;
	class Mesh;

	struct BLAS
	{
		vk::raii::Buffer ScratchBuffer = nullptr;
		vk::raii::DeviceMemory ScratchMemory = nullptr;
		vk::raii::Buffer Buffer = nullptr;
		vk::raii::DeviceMemory Memory = nullptr;
		vk::raii::AccelerationStructureKHR Handle = nullptr;
		vk::DeviceAddress DeviceAddress = 0;
	};

	struct Instances
	{
		vk::raii::Buffer Buffer = nullptr;
		vk::raii::DeviceMemory Memory = nullptr;
		std::vector<vk::AccelerationStructureInstanceKHR> InstanceData;
		uint32_t AllocatedInstances = 0;
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

		void BuildAccelerationStructures(const Ref<Scene>& scene, const vk::raii::CommandBuffer& cmd, uint32_t frameIndex);

		vk::AccelerationStructureKHR GetTLAS(uint32_t frameIndex) const;

	private:
		void UpdateTLAS(const Ref<Scene>& scene);
		void BuildBLAS(const vk::raii::CommandBuffer& cmd, Mesh* mesh);

		static void InsertTLASBarrier(const vk::raii::CommandBuffer& cmd);
		static void InsertBLASBarrier(const vk::raii::CommandBuffer& cmd);

	private:
		std::unordered_map<Mesh*, BLAS> m_BLASCache;
		std::array<Instances, VulkanContext::MaxFramesInFlight> m_InstancesCache;
		std::array<TLAS, VulkanContext::MaxFramesInFlight> m_TLASCache;
	};
}
