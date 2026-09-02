#include "RayTracingSceneCache.hpp"
#include "Core/Core.hpp"

#include "Core/Timer.hpp"
#include "Scene/Scene.hpp"
#include "Scene/Entity.hpp"
#include "Profiling/Instrumentor.hpp"

import Kerberos;

namespace
{
    vk::TransformMatrixKHR ToVkTransformMatrix(const glm::mat4& transform)
    {
        vk::TransformMatrixKHR tm{};
        tm.matrix[0][0] = transform[0][0];
        tm.matrix[0][1] = transform[1][0];
        tm.matrix[0][2] = transform[2][0];
        tm.matrix[0][3] = transform[3][0];
        tm.matrix[1][0] = transform[0][1];
        tm.matrix[1][1] = transform[1][1];
        tm.matrix[1][2] = transform[2][1];
        tm.matrix[1][3] = transform[3][1];
        tm.matrix[2][0] = transform[0][2];
        tm.matrix[2][1] = transform[1][2];
        tm.matrix[2][2] = transform[2][2];
        tm.matrix[2][3] = transform[3][2];
        return tm;
	}
}

namespace Kerberos
{
	void RayTracingSceneCache::InsertTLASBarrier(const vk::raii::CommandBuffer& cmd) 
    {
		static constexpr vk::MemoryBarrier2 tlasToShaderBarrier{
			.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
			.srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
			.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
			.dstAccessMask = vk::AccessFlagBits2::eShaderRead
		};

		constexpr vk::DependencyInfo dependencyInfo{
			.dependencyFlags = {},
			.memoryBarrierCount = 1,
			.pMemoryBarriers = &tlasToShaderBarrier,
		};

		cmd.pipelineBarrier2(dependencyInfo);
	}

	void RayTracingSceneCache::InsertBLASBarrier(const vk::raii::CommandBuffer& cmd) 
    {
        static constexpr vk::MemoryBarrier2 blasToTlasBarrier{
			.srcStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .srcAccessMask = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
			.dstStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
            .dstAccessMask = vk::AccessFlagBits2::eAccelerationStructureReadKHR
        };

        constexpr vk::DependencyInfo dependencyInfo{
            .dependencyFlags = {},
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &blasToTlasBarrier,
        };

        cmd.pipelineBarrier2(dependencyInfo);
	}

	void RayTracingSceneCache::BuildAccelerationStructures(const Ref<Scene>& scene, const vk::raii::CommandBuffer& cmd, const uint32_t frameIndex)
	{
        KBR_PROFILE_FUNCTION();

		KBRAssert(scene != nullptr, "Scene cannot be null!");

        auto& context = VulkanContext::Get();
        const auto& device = context.GetDevice();

        auto& instancesCache = m_InstancesCache[frameIndex];
        instancesCache.InstanceData.clear();

        bool anyBlasBuilt = false;

		const auto view = scene->m_Registry.view<TransformComponent, StaticMeshComponent>();
        for (const auto entity : view)
        {
			auto [transformComp, staticMeshComp] = view.get<TransformComponent, StaticMeshComponent>(entity);
			const Ref<Mesh>& meshRef = staticMeshComp.StaticMesh;

			if (!meshRef)
            {
                Log::CoreWarn("Entity {} has a StaticMeshComponent with a null StaticMesh reference. Skipping acceleration structure build for this entity.", static_cast<uint32_t>(entity));
                continue;
			}

			Mesh* mesh = meshRef.get();
            if (!m_BLASCache.contains(mesh))
            {
                BuildBLAS(cmd, mesh);
                anyBlasBuilt = true;
            }

            vk::AccelerationStructureInstanceKHR instance{
                .transform = ToVkTransformMatrix(transformComp.GetTransform()),
                .instanceCustomIndex = static_cast<uint32_t>(entity),
                .mask = 0xFF,
                .flags = static_cast<uint32_t>(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
                .accelerationStructureReference = m_BLASCache[mesh].DeviceAddress
            };
            instancesCache.InstanceData.push_back(instance);
        }

        if (instancesCache.InstanceData.empty())
            return;

        // Wait for BLAS builds to finish before reading them to build the TLAS
        if (anyBlasBuilt)
        {
            InsertBLASBarrier(cmd);
        }

        const vk::DeviceSize instBufferSize = sizeof(instancesCache.InstanceData[0]) * instancesCache.InstanceData.size();

        if (instancesCache.AllocatedInstances < instancesCache.InstanceData.size())
        {
            instancesCache.AllocatedInstances = static_cast<uint32_t>(instancesCache.InstanceData.size());
            instancesCache.Buffer = nullptr;
            instancesCache.Memory = nullptr;

            CreateBuffer(device,
                         instBufferSize,
                         vk::BufferUsageFlagBits::eShaderDeviceAddress |
                         vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                         vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                         instancesCache.Buffer, instancesCache.Memory);
        }

        void* ptr = instancesCache.Memory.mapMemory(0, instBufferSize);
        memcpy(ptr, instancesCache.InstanceData.data(), instBufferSize);
        instancesCache.Memory.unmapMemory();

        vk::BufferDeviceAddressInfo instanceAddrInfo{ .buffer = instancesCache.Buffer };
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
            .flags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace, /* | vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate, */
            .mode = vk::BuildAccelerationStructureModeKHR::eBuild,
            .geometryCount = 1,
            .pGeometries = &tlasGeometry 
        };

        const uint32_t primitiveCount = static_cast<uint32_t>(instancesCache.InstanceData.size());

        vk::AccelerationStructureBuildSizesInfoKHR tlasBuildSizes =
            device.getAccelerationStructureBuildSizesKHR(
                vk::AccelerationStructureBuildTypeKHR::eDevice,
                tlasBuildGeometryInfo,
                { primitiveCount });

        TLAS& tlasData = m_TLASCache[frameIndex];

        // Need to recreate TLAS or Scratch buffer if too small
        if (tlasData.Handle == nullptr || tlasData.Buffer == nullptr)
        {
            tlasData.ScratchBuffer = nullptr;
            tlasData.Buffer = nullptr;
            tlasData.Handle = nullptr;

            CreateBuffer(device, tlasBuildSizes.buildScratchSize,
                         vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                         tlasData.ScratchBuffer, tlasData.ScratchMemory);

            CreateBuffer(device, tlasBuildSizes.accelerationStructureSize,
                         vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                         vk::MemoryPropertyFlagBits::eDeviceLocal,
                         tlasData.Buffer, tlasData.Memory);

            vk::AccelerationStructureCreateInfoKHR tlasCreateInfo{
                .buffer = tlasData.Buffer,
                .size = tlasBuildSizes.accelerationStructureSize,
                .type = vk::AccelerationStructureTypeKHR::eTopLevel,
            };
            tlasData.Handle = device.createAccelerationStructureKHR(tlasCreateInfo);
        }

        vk::BufferDeviceAddressInfo scratchAddressInfo{ .buffer = tlasData.ScratchBuffer };
        tlasBuildGeometryInfo.scratchData.deviceAddress = device.getBufferAddressKHR(scratchAddressInfo);
        tlasBuildGeometryInfo.dstAccelerationStructure = tlasData.Handle;

        vk::AccelerationStructureBuildRangeInfoKHR tlasRangeInfo{
            .primitiveCount = primitiveCount
        };

        cmd.buildAccelerationStructuresKHR({ tlasBuildGeometryInfo }, { &tlasRangeInfo });

        InsertTLASBarrier(cmd);
	}

	vk::AccelerationStructureKHR RayTracingSceneCache::GetTLAS(const uint32_t frameIndex) const
    {
		return *m_TLASCache[frameIndex].Handle;
	}

	void RayTracingSceneCache::UpdateTLAS([[maybe_unused]] const Ref<Scene>& scene)
    {
		
	}

	void RayTracingSceneCache::BuildBLAS(const vk::raii::CommandBuffer& cmd, Mesh* mesh)
    {
		KBRAssert(mesh, "Mesh cannot be null!");

		auto& context = VulkanContext::Get();
		const auto& device = context.GetDevice();

        auto trianglesData = vk::AccelerationStructureGeometryTrianglesDataKHR{
            .vertexFormat = vk::Format::eR32G32B32Sfloat,
            .vertexData = mesh->GetVertexBuffer().GetDeviceAddress(),
            .vertexStride = sizeof(Vertex),
            .maxVertex = static_cast<uint32_t>(mesh->GetVertices().size() - 1),
            .indexType = vk::IndexType::eUint32,
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

        BLAS blas{};
        CreateBuffer(device, blasBuildSizes.buildScratchSize,
                     vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                     vk::MemoryPropertyFlagBits::eDeviceLocal,
                     blas.ScratchBuffer, blas.ScratchMemory);

        vk::BufferDeviceAddressInfo scratchAddressInfo{ .buffer = *blas.ScratchBuffer };
        blasBuildGeometryInfo.scratchData.deviceAddress = device.getBufferAddressKHR(scratchAddressInfo);

        CreateBuffer(device,
                     blasBuildSizes.accelerationStructureSize,
                     vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                     vk::BufferUsageFlagBits::eShaderDeviceAddress |
                     vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR,
                     vk::MemoryPropertyFlagBits::eDeviceLocal,
                     blas.Buffer, blas.Memory);

        vk::AccelerationStructureCreateInfoKHR blasCreateInfo{
            .buffer = blas.Buffer,
            .size = blasBuildSizes.accelerationStructureSize,
            .type = vk::AccelerationStructureTypeKHR::eBottomLevel,
        };
        blas.Handle = vk::raii::AccelerationStructureKHR(device, blasCreateInfo);
        blasBuildGeometryInfo.dstAccelerationStructure = *blas.Handle;

        vk::AccelerationStructureBuildRangeInfoKHR blasRangeInfo{
            .primitiveCount = primitiveCount
        };

        cmd.buildAccelerationStructuresKHR({ blasBuildGeometryInfo }, { &blasRangeInfo });

        vk::AccelerationStructureDeviceAddressInfoKHR addrInfo{ .accelerationStructure = blas.Handle };
        blas.DeviceAddress = device.getAccelerationStructureAddressKHR(addrInfo);

        m_BLASCache[mesh] = std::move(blas);
	}
}