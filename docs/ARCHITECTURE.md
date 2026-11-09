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
scene        EnTT world: transforms, hierarchy, scene files     asset  cooked formats, AssetManager (async, ref-counted, hot reload), glTF/HDR importers
debugui      Dear ImGui theme + reflection-driven inspector
rhi          Vulkan device, swapchain, bindless table, command lists, resources
platform     window, input, files, threads, time
core         types, log, assert, handles, memory (allocators + tracking), jobs, math, Guid, reflection, JSON

tools/       ember_cooker (links core + asset only), fetch-sample-content.ps1
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

## Asset pipeline

```
content/ (glb/gltf/hdr + .meta Guid) -> ember_cooker -> build/cooked/<guid>.emodel|.eenv + manifest.json -> AssetManager
                                          (mip chains now; meshlets,     (skips sources whose content     (loads on worker threads,
                                           LODs, BC7 later)               hash didn't change)              ref counts, hot reload)
```

- Everything references assets by **Guid**, never by path. The Guid lives in the `.meta` file next to the source.
- The `ember_cook` build target runs the cooker on every build; it's incremental, so it's quick.
- Hot reload: the AssetManager watches the sources of loaded assets, runs the cooker on a changed one
  and reloads it. The game sees a new `version()` and re-uploads.

## Scene and game state

- `scene::Scene` wraps an EnTT registry. Every entity has an `IdComponent` (Guid), a name, a local
  `Transform`, a computed `WorldTransform` and `Hierarchy` links (parent / first child / siblings).
- Components are plain reflected structs (`EMBER_REFLECT_*`), so scene files, the inspector and
  anything else generic come for free. Register game components with `Scene::registerComponent<T>()`.
- Each frame the game runs systems (plain functions over component views), then *extracts* a flat
  `render::RenderScene`. The renderer never sees entities.

## Memory

- `memory::allocate` counts bytes per `memory::Tag`; `TrackedAllocator` does the same for std containers
  (asset data, job queues and the render scene use it). The Engine panel shows live/peak per tag plus GPU memory.
- `LinearAllocator` / `FrameAllocator` for scratch data (the renderer's draw list lives in frame memory),
  `PoolAllocator<T>` for many same-sized objects.
