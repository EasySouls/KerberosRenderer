@echo off

set SLANGC=%VULKAN_SDK%\bin\slangc.exe

echo Using Slang compiler: %SLANGC%

if not exist "%SLANGC%" (
    echo ERROR: slangc.exe not found at "%SLANGC%". Is VULKAN_SDK set correctly?
    pause
    exit /b 1
)

"%SLANGC%" simple.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/simple.spv
"%SLANGC%" shadowmap.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/shadowmap.spv
"%SLANGC%" pbrbasic.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/pbrbasic.spv
"%SLANGC%" pbrtextured.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/pbrtextured.spv
"%SLANGC%" skybox.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/skybox.spv
"%SLANGC%" genbrdflut.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/genbrdflut.spv
"%SLANGC%" irradiancecube.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/irradiancecube.spv
"%SLANGC%" prefilterenvmap.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../cache/shaders/prefilterenvmap.spv
"%SLANGC%" normaldebug.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry geometryMain -entry fragmentMain -o ../cache/shaders/normaldebug.spv

PAUSE