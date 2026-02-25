#include "pch.hpp"
#include "MemoryBudget.hpp"

#include "VulkanContext.hpp"

namespace kbr
{
	constexpr float kilobyteCoefficient = 1024.0f;
	constexpr float megabyteCoefficient = kilobyteCoefficient * 1024.0f;
	constexpr float gigabyteCoefficient = megabyteCoefficient * 1024.0f;

	ConvertedMemory MemoryBudget::ConvertBytes(const uint64_t bytes) {
		ConvertedMemory converted{};

		const auto memory = static_cast<float>(bytes);

		if (memory < kilobyteCoefficient)
		{
			converted.data = memory;
			converted.units = "B";
		}
		else if (memory < megabyteCoefficient)
		{
			converted.data = memory / kilobyteCoefficient;
			converted.units = "KB";
		}
		else if (memory < gigabyteCoefficient)
		{
			converted.data = memory / megabyteCoefficient;
			converted.units = "MB";
		}
		else
		{
			converted.data = memory / gigabyteCoefficient;
			converted.units = "GB";
		}

		return converted;
	}

	MemoryBudgetInfo MemoryBudget::GetMemoryBudgetInfo() const
	{
		return m_MemoryBudgetInfo;
	}

	void MemoryBudget::UpdateMemoryBudgetInfo()
	{
		const auto result = VulkanContext::Get().GetPhysicalDevice().getMemoryProperties2<vk::PhysicalDeviceMemoryProperties2, vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();
		const vk::PhysicalDeviceMemoryProperties2& deviceMemoryProps = result.get<vk::PhysicalDeviceMemoryProperties2>();
		const vk::PhysicalDeviceMemoryBudgetPropertiesEXT& memoryBudgetProps = result.get<vk::PhysicalDeviceMemoryBudgetPropertiesEXT>();

		uint64_t deviceMemoryTotalUsage = 0;
		uint64_t deviceMemoryTotalBudget = 0;
		const uint64_t deviceMemoryHeapCount = deviceMemoryProps.memoryProperties.memoryHeapCount;

		for (int i = 0; i < static_cast<int>(deviceMemoryHeapCount); ++i)
		{
			deviceMemoryTotalUsage += memoryBudgetProps.heapUsage[i];
			deviceMemoryTotalBudget += memoryBudgetProps.heapBudget[i];
		}

		m_MemoryBudgetInfo = MemoryBudgetInfo{
			.DeviceMemoryTotalUsage = deviceMemoryTotalUsage,
			.DeviceMemoryTotalBudget = deviceMemoryTotalBudget,
			.DeviceMemoryHeapCount = deviceMemoryHeapCount,
			.MemoryBudgetProps = memoryBudgetProps,
			.DeviceMemoryProps = deviceMemoryProps.memoryProperties
		};
	}

	std::string MemoryBudget::ReadMemoryHeapFlags(const vk::MemoryHeapFlags inputVkMemoryHeapFlag)
	{
		if (inputVkMemoryHeapFlag == vk::MemoryHeapFlagBits::eDeviceLocal)
		{
			return "Device Local Bit";
		}
		if (inputVkMemoryHeapFlag == vk::MemoryHeapFlagBits::eMultiInstance)
		{
			return "Multiple Instance Bit";
		}

		// In case that it does not correspond to device local memory
		return "Host Local Heap Memory";
	}
}