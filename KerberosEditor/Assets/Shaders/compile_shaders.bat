@echo off

set SLANGC=%VULKAN_SDK%\bin\slangc.exe

echo Using Slang compiler: %SLANGC%

if not exist "%SLANGC%" (
    echo ERROR: slangc.exe not found at "%SLANGC%". Is VULKAN_SDK set correctly?
    pause
    exit /b 1
)

"%SLANGC%" simple.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/simple.spv
"%SLANGC%" shadowmap.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/shadowmap.spv
"%SLANGC%" pbrbasic.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/pbrbasic.spv
"%SLANGC%" pbrtextured.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/pbrtextured.spv
"%SLANGC%" skybox.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/skybox.spv
"%SLANGC%" genbrdflut.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/genbrdflut.spv
"%SLANGC%" irradiancecube.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/irradiancecube.spv
"%SLANGC%" prefilterenvmap.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry fragmentMain -o ../Cache/Shaders/prefilterenvmap.spv
"%SLANGC%" normaldebug.slang -target spirv -profile spirv_1_6 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertexMain -entry geometryMain -entry fragmentMain -o ../Cache/Shaders/normaldebug.spv

PAUSE