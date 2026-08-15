#include "DescriptorWriter.hpp"

#include "VulkanContext.hpp"
#include "kbrpch.hpp"

namespace Kerberos {
DescriptorWriter::DescriptorWriter(const vk::raii::DescriptorSetLayout& layout, ShaderResourceSet& set)
{
    m_Layout = &layout;
    m_Set = &set;
    if (auto& context = VulkanContext::Get(); context.UseDescriptorBuffers()) {
        m_UseDescriptorBuffers = true;
        m_DescProps =
            context.GetPhysicalDevice()
                .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDescriptorBufferPropertiesEXT>()
                .get<vk::PhysicalDeviceDescriptorBufferPropertiesEXT>();
    }
    else {
        m_UseDescriptorBuffers = false;
    }
}

void DescriptorWriter::WriteStorageBuffer(const uint32_t binding,
                                          const vk::Buffer& buffer,
                                          const vk::DeviceSize size,
                                          const vk::DeviceSize offset)
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    if (m_UseDescriptorBuffers) {
        const vk::DeviceSize bindingOffset = m_Layout->getBindingOffsetEXT(binding);

        const vk::DescriptorAddressInfoEXT addrInfo{ .address = device.getBufferAddress({ .buffer = buffer }) + offset,
                                                     .range = size,
                                                     .format = vk::Format::eUndefined };
        const vk::DescriptorGetInfoEXT getInfo{ .type = vk::DescriptorType::eStorageBuffer,
                                                .data = vk::DescriptorDataEXT(&addrInfo) };

        device.getDescriptorEXT(
            getInfo, m_DescProps.storageBufferDescriptorSize, static_cast<uint8_t*>(m_Set->MappedData) + bindingOffset);
    }
    else {
        m_BufferInfos.push_back({ .buffer = buffer, .offset = offset, .range = size });
        m_Writes.push_back({ .dstSet = m_Set->DescriptorSet,
                             .dstBinding = binding,
                             .dstArrayElement = 0,
                             .descriptorCount = 1,
                             .descriptorType = vk::DescriptorType::eStorageBuffer,
                             .pImageInfo = nullptr,
                             .pBufferInfo = &m_BufferInfos.back(),
                             .pTexelBufferView = nullptr });
    }
}

void DescriptorWriter::WriteUniformBuffer(const uint32_t binding,
                                          const vk::Buffer& buffer,
                                          const vk::DeviceSize size,
                                          const vk::DeviceSize offset)
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    if (m_UseDescriptorBuffers) {
        const vk::DeviceSize bindingOffset = m_Layout->getBindingOffsetEXT(binding);

        const vk::DescriptorAddressInfoEXT addrInfo{ .address = device.getBufferAddress({ .buffer = buffer }) + offset,
                                                     .range = size,
                                                     .format = vk::Format::eUndefined };
        const vk::DescriptorGetInfoEXT getInfo{ .type = vk::DescriptorType::eUniformBuffer,
                                                .data = vk::DescriptorDataEXT(&addrInfo) };

        device.getDescriptorEXT(
            getInfo, m_DescProps.uniformBufferDescriptorSize, static_cast<uint8_t*>(m_Set->MappedData) + bindingOffset);
    }
    else {
        m_BufferInfos.push_back({ .buffer = buffer, .offset = offset, .range = size });
        m_Writes.push_back({ .dstSet = m_Set->DescriptorSet,
                             .dstBinding = binding,
                             .dstArrayElement = 0,
                             .descriptorCount = 1,
                             .descriptorType = vk::DescriptorType::eUniformBuffer,
                             .pImageInfo = nullptr,
                             .pBufferInfo = &m_BufferInfos.back(),
                             .pTexelBufferView = nullptr });
    }
}

void DescriptorWriter::WriteSampledImage(const uint32_t binding,
                                         const vk::ImageView& imageView,
                                         const vk::ImageLayout layout)
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    if (m_UseDescriptorBuffers) {
        const vk::DeviceSize bindingOffset = m_Layout->getBindingOffsetEXT(binding);

        vk::DescriptorImageInfo imageInfo{ .sampler = nullptr, .imageView = imageView, .imageLayout = layout };
        const vk::DescriptorGetInfoEXT imageGetInfo{ .type = vk::DescriptorType::eSampledImage,
                                                     .data = vk::DescriptorDataEXT(&imageInfo) };

        device.getDescriptorEXT(imageGetInfo,
                                m_DescProps.sampledImageDescriptorSize,
                                static_cast<uint8_t*>(m_Set->MappedData) + bindingOffset);
    }
    else {
        m_ImageInfos.push_back({ .sampler = nullptr, .imageView = imageView, .imageLayout = layout });
        m_Writes.push_back({ .dstSet = m_Set->DescriptorSet,
                             .dstBinding = binding,
                             .dstArrayElement = 0,
                             .descriptorCount = 1,
                             .descriptorType = vk::DescriptorType::eSampledImage,
                             .pImageInfo = &m_ImageInfos.back(),
                             .pBufferInfo = nullptr,
                             .pTexelBufferView = nullptr });
    }
}

void DescriptorWriter::WriteSampler(const uint32_t binding, const vk::Sampler& sampler)
{
    auto& context = VulkanContext::Get();
    const auto& device = context.GetDevice();

    if (m_UseDescriptorBuffers) {
        const vk::DeviceSize bindingOffset = m_Layout->getBindingOffsetEXT(binding);

        vk::DescriptorImageInfo imageInfo{ .sampler = sampler,
                                           .imageView = nullptr,
                                           .imageLayout = vk::ImageLayout::eUndefined };
        const vk::DescriptorGetInfoEXT imageGetInfo{ .type = vk::DescriptorType::eSampler,
                                                     .data = vk::DescriptorDataEXT(&imageInfo) };

        device.getDescriptorEXT(
            imageGetInfo, m_DescProps.samplerDescriptorSize, static_cast<uint8_t*>(m_Set->MappedData) + bindingOffset);
    }
    else {
        m_ImageInfos.push_back(
            { .sampler = sampler, .imageView = nullptr, .imageLayout = vk::ImageLayout::eUndefined });
        m_Writes.push_back({ .dstSet = m_Set->DescriptorSet,
                             .dstBinding = binding,
                             .dstArrayElement = 0,
                             .descriptorCount = 1,
                             .descriptorType = vk::DescriptorType::eSampler,
                             .pImageInfo = &m_ImageInfos.back(),
                             .pBufferInfo = nullptr,
                             .pTexelBufferView = nullptr });
    }
}

void DescriptorWriter::Flush()
{
    if (!m_UseDescriptorBuffers && !m_Writes.empty()) {
        VulkanContext::Get().GetDevice().updateDescriptorSets(m_Writes, nullptr);

        m_Writes.clear();
        m_BufferInfos.clear();
        m_ImageInfos.clear();
    }
}
} // namespace Kerberos
