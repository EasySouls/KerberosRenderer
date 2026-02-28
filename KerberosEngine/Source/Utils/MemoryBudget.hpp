#pragma once

#include "Vulkan.hpp"

#include <string>

namespace Kerberos
{
	struct MemoryBudgetInfo
	{
		uint64_t DeviceMemoryTotalUsage;
		uint64_t DeviceMemoryTotalBudget;
		uint64_t DeviceMemoryHeapCount;
		vk::PhysicalDeviceMemoryBudgetPropertiesEXT MemoryBudgetProps;
		vk::PhysicalDeviceMemoryProperties DeviceMemoryProps;
	};

	struct ConvertedMemory
	{
		float       data;
		std::string units;
	};

	class MemoryBudget
	{
	public:
		static ConvertedMemory ConvertBytes(uint64_t bytes);
		static std::string ReadMemoryHeapFlags(vk::MemoryHeapFlags inputVkMemoryHeapFlag);
		MemoryBudgetInfo GetMemoryBudgetInfo() const;

		void UpdateMemoryBudgetInfo();

	private:

	private:
		MemoryBudgetInfo m_MemoryBudgetInfo{};
	};
}