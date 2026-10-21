#include "NativeUpscaler.hpp"

#include <array>

import Kerberos;

namespace Kerberos {

    NativeUpscaler::NativeUpscaler(const vk::Device /*device*/)
    {
    }


    void NativeUpscaler::Initialize(const UpscalerCreateInfo& /*settings*/)
    {
    }

    void NativeUpscaler::Release()
    {
    }

    void NativeUpscaler::Resize(const uint32_t /*displayWidth*/, const uint32_t /*displayHeight*/)
    {
    }

    void NativeUpscaler::BeginFrame(const UpscalerFrame& /*frame*/)
    {
    }

    void NativeUpscaler::Dispatch(const UpscalerDispatchInfo& dispatchInfo)
    {
        if (dispatchInfo.commandBuffer == nullptr || dispatchInfo.inputColor.image == nullptr ||
            dispatchInfo.outputColor.image == nullptr)
            return;

        const vk::ImageMemoryBarrier2 inputBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits2::eShaderRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
            .oldLayout = dispatchInfo.inputColor.currentLayout,
            .newLayout = vk::ImageLayout::eTransferSrcOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dispatchInfo.inputColor.image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 outputBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
            .srcAccessMask = {},
            .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .oldLayout = dispatchInfo.outputColor.currentLayout,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dispatchInfo.outputColor.image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const std::array barriers = { inputBarrier, outputBarrier };
        dispatchInfo.commandBuffer.pipelineBarrier2(
            { .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
              .pImageMemoryBarriers = barriers.data() });

        const vk::ImageCopy copyRegion{
            .srcSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                .mipLevel = 0,
                                .baseArrayLayer = 0,
                                .layerCount = 1 },
            .srcOffset = {},
            .dstSubresource = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                .mipLevel = 0,
                                .baseArrayLayer = 0,
                                .layerCount = 1 },
            .dstOffset = {},
            .extent = { dispatchInfo.outputColor.width, dispatchInfo.outputColor.height, 1 }
        };
        dispatchInfo.commandBuffer.copyImage(dispatchInfo.inputColor.image,
                                             vk::ImageLayout::eTransferSrcOptimal,
                                             dispatchInfo.outputColor.image,
                                             vk::ImageLayout::eTransferDstOptimal,
                                             copyRegion);

        const vk::ImageMemoryBarrier2 finalBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dispatchInfo.outputColor.image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const vk::ImageMemoryBarrier2 inputFinalBarrier{
            .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
            .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
            .dstAccessMask = vk::AccessFlagBits2::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = dispatchInfo.inputColor.image,
            .subresourceRange = { .aspectMask = vk::ImageAspectFlagBits::eColor,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1 }
        };
        const std::array finalBarriers = { finalBarrier, inputFinalBarrier };
        dispatchInfo.commandBuffer.pipelineBarrier2({ .imageMemoryBarrierCount =
                                                           static_cast<uint32_t>(finalBarriers.size()),
                                                       .pImageMemoryBarriers = finalBarriers.data() });
    }

    void NativeUpscaler::SetQuality(const UpscalerQuality /*quality*/)
    {
    }

    glm::vec2 NativeUpscaler::GetJitterOffset() const
    {
        return glm::vec2(0.0f, 0.0f);
    }

    float NativeUpscaler::GetInverseUpscaleRatio() const
    {
        return 1.0f;
    }

}
