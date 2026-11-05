# Ember Roadmap

Each phase ends with something you can see or measure. Phases 5–8 build the foundations of UE5's
headline features (Nanite, Lumen, Virtual Shadow Maps, TSR).

## Phase 0: Foundations
- [x] CMake + vcpkg project, module layout
- [x] Logging, asserts, `Handle`/`SlotMap` + unit tests
- [x] SDL3 window
- [x] Vulkan 1.3 device, swapchain, frames in flight, clear screen
- [ ] Install the Vulkan SDK and confirm validation is clean
- [x] Tracy profiler integration (`EMBER_PROFILE_*` macros in `core/Profile.h`)
- [x] Dear ImGui overlay (SDL3 input in `platform`, Vulkan drawing in `rhi`)

## Phase 1: Basic renderer
- [ ] RHI resources: buffers, textures, pipelines, command lists
- [ ] Slang shader compilation (offline tool + hot reload)
- [ ] Triangle, then glTF meshes with a camera
- [ ] PBR materials (GGX), image-based lighting
- [ ] Directional shadow map, tonemapping

## Phase 2: Engine core
- [ ] Allocators (frame/linear, pool, general) + memory tracking
- [ ] Job system (work-stealing, counters)
- [ ] ECS (EnTT), transforms and hierarchy
- [ ] Asset GUIDs, cooker v1, async loading, hot reload
- [ ] Reflection + serialization

## Phase 3: Modern RHI
- [ ] Bindless descriptors
- [ ] Render graph (barriers, transient aliasing)
- [ ] Async compute + transfer queues

## Phase 4: Lighting at scale
- [ ] Clustered forward+ lighting
- [ ] Cascaded shadow maps
- [ ] TAA, bloom, SSAO, auto-exposure

## Phase 5: GPU-driven rendering (base for Nanite)
- [ ] Meshlets (meshoptimizer)
- [ ] GPU frustum + two-pass Hi-Z occlusion culling
- [ ] Indirect draws / mesh shaders

## Phase 6: Virtualized geometry (Nanite core)
- [ ] Visibility buffer + material resolve
- [ ] Cluster LOD DAG (simplify + METIS grouping)
- [ ] Compute software rasterizer for tiny triangles
- [ ] Cluster page streaming

## Phase 7: Dynamic global illumination (Lumen core)
- [ ] Screen-space GI
- [ ] DDGI (ray-traced probe grid)
- [ ] Global SDF + surface cache, hybrid screen/SDF/HW-RT tracing

## Phase 8: Virtual shadow maps + temporal upscaling
- [ ] Sparse paged shadow maps with caching
- [ ] TAA upscaler (TSR-like); FSR/DLSS integration

## Phase 9: Game layer
- [ ] Jolt physics, animation, audio (miniaudio), scripting, ImGui editor

## Phase 10: Scale
- [ ] World streaming + HLOD, virtual texturing, hardware ray tracing, networking

## Key references
- *Real-Time Rendering* (4th ed.), *Game Engine Architecture* (Gregory)
- Karis, "Nanite: A Deep Dive" (SIGGRAPH 2021)
- Wright, "Lumen: Real-time Global Illumination in UE5" (SIGGRAPH 2022)
- Majercik et al., "Dynamic Diffuse Global Illumination"
- O'Donnell, "FrameGraph" (GDC 2017); Haar & Aaltonen, "GPU-Driven Rendering Pipelines" (SIGGRAPH 2015)
- Code to read: Godot 4, The Forge, Wicked Engine, zeux/niagara, UE5 source
