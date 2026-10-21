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
cmake --build --preset vs2026-debug --target KerberosEngineTests KerberosIntegrationTests KerberosEditorE2ETests
```

Run all discovered tests from the generated build directory:

```powershell
ctest --test-dir out\build\vs2026 -C Debug --output-on-failure
```

The tests are grouped by lifecycle depth:

- `KerberosEngineTests` validates project deserialization and activation
  without a graphics context.
- `KerberosIntegrationTests` uses `TestApplication` with a hidden GLFW window,
  Vulkan, renderer, scripting, and the real editor asset-manager load path.
- `KerberosEditorE2ETests` additionally deserializes the configured start scene
  and verifies that it contains entities.

The integration and end-to-end tests require a working Vulkan SDK/device and
the generated `KerberosEditor\Resources\Scripts` assembly. They can take
longer than unit tests because the renderer initializes shaders and GPU
resources.

Run an individual level with:

```powershell
ctest --test-dir out\build\vs2026 -C Debug -R KerberosEngineTests --output-on-failure
ctest --test-dir out\build\vs2026 -C Debug -R ProjectIntegrationTest --output-on-failure
ctest --test-dir out\build\vs2026 -C Debug -R EditorEndToEndTest --output-on-failure
```

The project fixture is `KerberosEditor\TestProject.kbrproj`; keep it and its
`KerberosEditor\Assets` directory available.

Set `-DKBR_BUILD_TESTS=OFF` when configuring a build that should not fetch or
build the GoogleTest target.
