#include "kbrpch.hpp"
#include "RayTracingSceneCache.hpp"

#include "Core/Timer.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"

namespace Kerberos
{
	void RayTracingSceneCache::BuildAccelerationStructures(const Ref<Scene>& scene) 
	{
		KBR_CORE_ASSERT(scene, "Scene cannot be null!");

		// For now only generate AS-s once
        if (!m_BLASCache.empty())
            return;

        Timer timer("Building Acceleration Structures", [&](const TimerData& data)
        {
            KBR_CORE_INFO("Building Acceleration Structures took {:.2f} ms", data.DurationMs);
        });

		std::set<Ref<Mesh>> uniqueMeshes;

		const auto view = scene->m_Registry.view<StaticMeshComponent>();
        for (const auto entity : view)
        {
			const auto& staticMeshComponent = view.get<StaticMeshComponent>(entity);
			const Ref<Mesh>& mesh = staticMeshComponent.StaticMesh;

			uniqueMeshes.insert(mesh);
        }

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

        for (const auto& mesh : uniqueMeshes)
        {
            auto trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR{
                .vertexFormat = vk::Format::eR32G32B32Sfloat,
                .vertexData = mesh->GetVertexBuffer().GetDeviceAddress(),
                .vertexStride = sizeof(Vertex),
                .maxVertex = static_cast<uint32_t>(mesh->GetVertices().size() - 1),
                .indexType = vk::IndexType::eUint32,
                //.indexData = indexAddr + submesh.indexOffset * sizeof(uint32_t)
                .indexData = mesh->GetIndexBuffer().GetDeviceAddress()
            };

            vk::AccelerationStructureGeometryDataKHR geometryData(trianglesData);

            vk::AccelerationStructureGeometryKHR blasGeometry{
                .geometryType = vk::GeometryTypeKHR::eTriangles,
                .geometry = geometryData,
                .flags = vk::GeometryFlagBitsKHR::eOpaque
            };

            vk::AccelerationStructureBuildGeometryInfoKHR blasBuildGeometryInfo{
                .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
                .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
                .geometryCount = 1,
                .pGeometries = &blasGeometry,
            };

            auto primitiveCount = static_cast<uint32_t>(mesh->GetIndices().size() / 3);

            vk::AccelerationStructureBuildSizesInfoKHR blasBuildSizes =
                device.getAccelerationStructureBuildSizesKHR(
                    vk::AccelerationStructureBuildTypeKHR::eDevice,
                    blasBuildGeometryInfo,
                    { primitiveCount }
                );

            vk::raii::Buffer       scratchBuffer = nullptr;
            vk::raii::DeviceMemory scratchMemory = nullptr;
            CreateBuffer(device, blasBuildSizes.buildScratchSize,
                         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                         scratchBuffer, scratchMemory);

            vk::BufferDeviceAddressInfo scratchAddressInfo{ .buffer = *scratchBuffer };
            vk::DeviceAddress           scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
            blasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

            BLAS blas{};

            CreateBuffer(device, 
                         blasBuildSizes.accelerationStructureSize,
                         vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                         blas.Buffer, blas.Memory);

            vk::AccelerationStructureCreateInfoKHR blasCreateInfo{
                .buffer = blas.Buffer,
                .offset = 0,
                .size = blasBuildSizes.accelerationStructureSize,
                .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
            };

			blas.Handle = vk::raii::AccelerationStructureKHR(device, blasCreateInfo);

            blasBuildGeometryInfo.dstAccelerationStructure = *blas.Handle;

            vk::AccelerationStructureBuildRangeInfoKHR blasRangeInfo{
                .primitiveCount = primitiveCount,
                .primitiveOffset = 0,
                .firstVertex = 0,
                .transformOffset = 0
            };

			auto cmd = context.BeginSingleTimeCommands();
			cmd.buildAccelerationStructuresKHR({ blasBuildGeometryInfo }, { &blasRangeInfo });
			context.EndSingleTimeCommands(cmd);

            vk::AccelerationStructureDeviceAddressInfoKHR addrInfo{
                .accelerationStructure = blas.Handle
            };
            vk::DeviceAddress blasDeviceAddr = device.getAccelerationStructureAddressKHR(addrInfo);

            // TODO: Get entity from scene
            vk::TransformMatrixKHR tm{};

            vk::AccelerationStructureInstanceKHR instance{
                .transform = tm,
                .mask = 0xFF,
                .accelerationStructureReference = blasDeviceAddr
            };

			m_InstancesCache.InstanceData.push_back(instance);

            m_BLASCache.push_back(std::move(blas));
        }

        vk::DeviceSize instBufferSize = sizeof(m_InstancesCache.InstanceData[0]) * m_InstancesCache.InstanceData.size();
        CreateBuffer(device, 
                     instBufferSize,
                     vk::BufferUsageFlagBits::eShaderDeviceAddress |
                        vk::BufferUsageFlagBits::eTransferDst |
                        vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                     m_InstancesCache.Buffer, m_InstancesCache.Memory);

        void* ptr = m_InstancesCache.Memory.mapMemory(0, instBufferSize);
        memcpy(ptr, m_InstancesCache.InstanceData.data(), instBufferSize);
        m_InstancesCache.Memory.unmapMemory();

        vk::BufferDeviceAddressInfo instanceAddrInfo{ .buffer = m_InstancesCache.Buffer };
        vk::DeviceAddress           instanceAddr = device.getBufferAddressKHR(instanceAddrInfo);

        auto instancesData = vk::AccelerationStructureGeometryInstancesDataKHR{
            .arrayOfPointers = vk::False,
            .data = instanceAddr };

        vk::AccelerationStructureGeometryDataKHR geometryData(instancesData);

        vk::AccelerationStructureGeometryKHR tlasGeometry{
            .geometryType = vk::GeometryTypeKHR::eInstances,
            .geometry = geometryData };

        vk::AccelerationStructureBuildGeometryInfoKHR tlasBuildGeometryInfo{
            .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            .flags = vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate,
            .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
            .geometryCount = 1,
            .pGeometries = &tlasGeometry 
        };

        const uint32_t primitiveCount = static_cast<uint32_t>(m_InstancesCache.InstanceData.size());

        vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes =
            device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                tlasBuildGeometryInfo,
                { primitiveCount });

        const auto cmd = context.BeginSingleTimeCommands();
        context.SetObjectDebugName(cmd, "TLAS Build Command Buffer");

        for (int i = 0; i < m_TLASCache.size(); ++i)
        {
            TLAS& tlasData = m_TLASCache[i];

            CreateBuffer(device,
               tlasBuildSizes.buildScratchSize,
               vk::BufferUsageFlagBits::eStorageBuffer |
               vk::BufferUsageFlagBits::eShaderDeviceAddress,
               vk::MemoryPropertyFlagBits::eDeviceLocal,
               tlasData.ScratchBuffer, tlasData.ScratchMemory);

            vk::BufferDeviceAddressInfo scratchAddressInfo{ .buffer = tlasData.ScratchBuffer };
            vk::DeviceAddress           scratchAddr = device.getBufferAddressKHR(scratchAddressInfo);
            tlasBuildGeometryInfo.scratchData.deviceAddress = scratchAddr;

            CreateBuffer(
                device,
                tlasBuildSizes.accelerationStructureSize,
                vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress |
                vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                vk::MemoryPropertyFlagBits::eDeviceLocal,
                tlasData.Buffer, tlasData.Memory);

            vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{
                .buffer = tlasData.Buffer,
                .offset = 0,
                .size = tlasBuildSizes.accelerationStructureSize,
                .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            };

            tlasData.Handle = device.createAccelerationStructureKHR(tlasCreateInfo);

            tlasBuildGeometryInfo.dstAccelerationStructure = tlasData.Handle;

            vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo{
                .primitiveCount = primitiveCount,
                .primitiveOffset = 0,
                .firstVertex = 0,
                .transformOffset = 0
            };

            cmd.buildAccelerationStructuresKHR({ tlasBuildGeometryInfo }, { &tlasRangeInfo });
        }

        context.EndSingleTimeCommands(cmd);
	}
}
