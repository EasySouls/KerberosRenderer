C:/VulkanSDK/1.4.335.0/bin/slangc.exe simple.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o simple.spv
C:/VulkanSDK/1.4.335.0/bin/slangc.exe shadowmap.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o shadowmap.spv
C:/VulkanSDK/1.4.335.0/bin/slangc.exe pbrbasic.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o pbrbasic.spv

PAUSE