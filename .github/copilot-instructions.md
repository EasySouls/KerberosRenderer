# KerberosRenderer repository instructions

## Project shape

KerberosRenderer is a C++23 Windows/Vulkan engine and editor built with CMake.
The engine is in `KerberosEngine/Source`, the editor is in
`KerberosEditor/Source`, and editor-authored assets and shaders are under
`KerberosEditor/Assets`.

Use the subsystem context files when working in their scopes:

- `.github/instructions/assets.instructions.md`
- `.github/instructions/renderer.instructions.md`
- `.github/instructions/particles.instructions.md`
- `.github/instructions/editor.instructions.md`

## Build and validation

The primary Windows workflow is:

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

Use the smallest affected target when possible. Shader changes also require
running `KerberosEditor\Assets\Shaders\compile_shaders.bat` with a configured
Vulkan SDK.

The code uses C++23, Vulkan-Hpp, Vulkan dynamic rendering, Slang shaders,
GLM, ImGui, YAML, and Tracy instrumentation. Preserve existing ownership,
logging, profiling, and error-reporting patterns.

## Source of truth and context maintenance

Always treat the source code as the source of truth. Do not rely on assumptions 
about the engine or editor behavior. If you are unsure, read the source code and
confirm the current implementation. The subsystem instruction files are architecture 
documentation, not static project history. Whenever logic, ownership, data flow, 
invariants, supported formats, synchronization, lifecycle, or validation commands 
change in the assets, renderer, particles, or editor systems, update the 
corresponding context file in the same change. Keep cross-system references 
synchronized as well, do not allow these files to describe behavior that 
the source no longer implements.