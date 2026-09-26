# Testing

Tests use GoogleTest, tracked as the
`KerberosEngine/ThirdParty/googletest` submodule and registered with CTest.
After cloning the repository, initialize all submodules:

```powershell
git submodule update --init --recursive
```

Configure and build the test target with the normal Windows preset:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug --target KerberosEngineTests
```

Run the discovered tests from the generated build directory:

```powershell
ctest --test-dir out\build\vs2026 -C Debug --output-on-failure
```

The initial sample test opens `KerberosEditor\TestProject.kbrproj` through the
engine `Project` API in headless mode. It validates project deserialization and
activation without requiring a GLFW window or Vulkan context. Normal editor
loads still initialize the asset manager, metadata, and asset watcher.

Set `-DKBR_BUILD_TESTS=OFF` when configuring a build that should not fetch or
build the GoogleTest target.
