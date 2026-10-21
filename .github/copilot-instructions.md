# KerberosRenderer repository instructions

Agent instructions for this repository live in `AGENTS.md` files. Read the
relevant one before changing code:

- `AGENTS.md` — repository shape, build/validation, and source-of-truth rules.
- `KerberosEngine/Source/Assets/AGENTS.md` — asset system, project asset
  loading, and the asset browser.
- `KerberosEngine/Source/Renderer/AGENTS.md` — renderer, Vulkan context and
  resources, the render graph, shaders, and the particle system.
- `KerberosEditor/AGENTS.md` — editor lifecycle, panels, and project/scene flow.

GitHub Copilot and other agents also load the nested `AGENTS.md` files
automatically for files inside those directories.

Always treat the source code as the source of truth. When logic, ownership,
data flow, invariants, supported formats, synchronization, lifecycle, or
validation commands change in the assets, renderer, particles, or editor
systems, update the corresponding `AGENTS.md` file in the same change.
