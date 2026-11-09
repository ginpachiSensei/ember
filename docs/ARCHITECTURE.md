# Ember Architecture

## Decisions

| Topic | Choice | Why |
|---|---|---|
| Language | C++20 | Matches almost all engine/graphics references |
| Graphics API | Vulkan 1.3 (dynamic rendering, synchronization2, timeline semaphores, buffer device address) | Explicit, cross-platform, modern feature set |
| Shaders | Slang (HLSL-compatible), compiled offline to SPIR-V | HLSL syntax used by most references, plus modules and generics |
| Packages | vcpkg manifest (`vcpkg.json`) | Dependencies build automatically on configure |
| ECS | EnTT to start, replaced by our own later | Don't block rendering work on the ECS |
| Windowing | SDL3, hidden inside `platform` | |
| GPU memory | Vulkan Memory Allocator (VMA), hidden inside `rhi` | |

## Principles

1. **Strict layering.** A module only depends on the modules below it.
2. **Data-oriented.** Plain structs in contiguous arrays and systems that loop over them. No deep inheritance.
3. **Handles, not pointers.** Resources are referenced by `Handle<T>` (index + generation), see `core/SlotMap.h`.
4. **Explicit memory.** Hot paths don't call hidden `new`. Allocations go through allocators (coming in phase 2).
5. **Parallel by default.** Work is expressed as jobs, not threads.
6. **Game state and render state are separate.** The game writes, and the renderer reads an extracted snapshot.
7. **Offline work happens offline.** The runtime loads cooked data. Importing and processing live in `tools/`.

Conventions: engine code doesn't throw exceptions (it returns errors or fails fast with `EMBER_VERIFY`), uses no RTTI, no `shared_ptr` and no singletons.

## Layers

```
apps/        sandbox, editor, game
runtime      engine loop, world, system scheduling          (planned)
render       forward PBR renderer: shadows, IBL, sky, tonemap; shader hot reload (render graph: phase 3)
physics / audio / anim                                      (planned)
scene        ECS, transforms (planned)     asset  glTF/HDR loading, procedural shapes (cooker + streaming: phase 2)
debugui      Dear ImGui theme (Inter + Font Awesome icons, ember-orange palette)
rhi          Vulkan device, swapchain, bindless table, command lists, resources
platform     window, input, files, threads, time
core         types, log, assert, containers, handles, jobs, math, reflection

tools/       cooker, shader compiler: separate executables, link core only (planned)
```

Rules:
- `core` depends on nothing.
- No Vulkan types appear in public `rhi` headers. The backend lives in `rhi/src/vulkan/`.
- `render` never sees the ECS. It consumes a flat **render scene** (instances, lights, cameras) that the runtime fills in.
- Tools never link the renderer or the runtime.

## Repo layout

```
engine/<module>/include/ember/<module>/   public headers  (#include "ember/core/Log.h")
engine/<module>/src/                      implementation
engine/<module>/tests/                    unit tests (doctest)
apps/                                     executables
shaders/                                  Slang sources; shaders/core/ = shared includes
tools/                                    scripts now (fetch-sample-content.ps1), offline tools later
content/                                  source assets (planned)
cmake/                                    build helpers (ember_configure_target)
```

Each module is one static library target: `ember::core`, `ember::platform`, `ember::rhi`, ...

## RHI design (target)

- Thin and explicit, shaped like Vulkan/D3D12 rather than OpenGL.
- **Bindless**: one global descriptor set (sampled images, samplers, storage images) and one pipeline
  layout with 128 bytes of push constants. Shaders index textures by integer and reach buffers
  through 64-bit device addresses (`Vertex* vertices` in Slang). No per-draw descriptor sets.
- Buffers and shaders share **scalar layout** (`-fvk-use-scalar-layout`), so C++ structs in
  `render/src/GpuTypes.h` match `shaders/core/Common.slang` field for field.
- **Texture states**: the RHI tracks each texture's state (`ResourceState`) and emits the barrier
  when you ask for a new one. Storage textures stay in GENERAL layout.
- **Conventions**: reverse-Z depth (1 = near, clear to 0, `Greater`), flipped viewport so clip
  space is y-up, counter-clockwise front faces (glTF).
- **Frames in flight** (2): per-frame command pools, fences and semaphores. Destruction is deferred until the GPU is done with a resource.
- Queues: graphics, async compute, transfer.
- The backend is chosen at compile time, so draws don't go through virtual calls.

`rhi::Device` owns the instance, device, swapchain, frame sync and every resource. Resources are handles; destroying one is deferred until the GPU has finished the frames that could use it.

## Renderer design (target)

```
ECS world --extract--> RenderScene --> Render Graph --> RHI
                       (flat arrays)    setup -> compile -> execute
```

- **Render graph**: passes declare the resources they read and write. The graph removes unused passes, aliases transient memory, inserts barriers and schedules async compute.
- **Features** (shadows, GI, post) each add passes, so a technique can be swapped (e.g. DDGI to SDF-based GI) without touching the rest.

## Frame model (target)

```
Frame N:    [game update N][extract N]
                                     [render-graph build/record N][GPU N]
Frame N+1:                 [game update N+1][extract N+1] ...
```

Fixed timestep for simulation, variable for rendering. Sync points: *extract* and GPU fences.

## Asset pipeline (target)

```
content/ (gltf, png + .meta GUID) -> tools/cooker -> cooked binaries + manifest -> runtime asset system
                                         (meshlets, LODs, BC7, derived-data cache)   (async IO, handles, refcount)
```
