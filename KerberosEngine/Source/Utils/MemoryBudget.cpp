#include "kbrpch.hpp"
#include "MemoryBudget.hpp"

#include "Renderer/VMA/VMA.hpp"
#include "Profiling/TracyInstrumentation.hpp"
#include "VulkanContext.hpp"

namespace Kerberos
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

#if defined(KBR_ENABLE_TRACY)
		// Publish VRAM usage to Tracy. Sum device-local heaps so multi-heap
		// configurations (for example ReBAR or APUs) report the full budget.
		const VmaAllocator allocator = VulkanContext::Get().GetAllocator().get();
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
		vmaGetHeapBudgets(allocator, budgets);

		constexpr double bytesPerMegabyte = 1024.0 * 1024.0;
		double deviceLocalUsedMB = 0.0;
		double deviceLocalBudgetMB = 0.0;
		bool hasDeviceLocalHeap = false;

		for (uint32_t heapIndex = 0; heapIndex < static_cast<uint32_t>(deviceMemoryHeapCount); ++heapIndex)
		{
			if (!(deviceMemoryProps.memoryProperties.memoryHeaps[heapIndex].flags & vk::MemoryHeapFlagBits::eDeviceLocal))
			{
				continue;
			}

			hasDeviceLocalHeap = true;
			deviceLocalUsedMB += static_cast<double>(budgets[heapIndex].usage) / bytesPerMegabyte;
			deviceLocalBudgetMB += static_cast<double>(budgets[heapIndex].budget) / bytesPerMegabyte;
		}

		if (!hasDeviceLocalHeap)
		{
			deviceLocalUsedMB = static_cast<double>(budgets[0].usage) / bytesPerMegabyte;
			deviceLocalBudgetMB = static_cast<double>(budgets[0].budget) / bytesPerMegabyte;
		}

		KBR_TRACY_PLOT("VRAM Used (MB)", deviceLocalUsedMB);
		KBR_TRACY_PLOT("VRAM Budget (MB)", deviceLocalBudgetMB);
#endif
	}

	std::string MemoryBudget::ReadMemoryHeapFlags(const vk::MemoryHeapFlags inputVkMemoryHeapFlag)
	{
		if (inputVkMemoryHeapFlag & vk::MemoryHeapFlagBits::eDeviceLocal)
		{
			return "Device Local Bit";
		}
		if (inputVkMemoryHeapFlag & vk::MemoryHeapFlagBits::eMultiInstance)
		{
			return "Multiple Instance Bit";
		}
		if (inputVkMemoryHeapFlag & vk::MemoryHeapFlagBits::eTileMemoryQCOM)
		{
			return "Tile Memory QCOM Bit";
		}

		// In case that it does not correspond to device local memory
		return "Host Local Heap Memory";
	}
}